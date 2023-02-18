# Cosmos NDP编程框架(easyNDP)说明

> 更新时间：2023-2-17
>
> 作者：Gary

### 一.简介

本文档主要用于说明本简易NDP框架——easyNDP framework的架构、开发新应用以及使用的方法。

在开始前，有一个概念需要提前说明，文档中的块这个概念，对应的是主机中的块/扇区/sector或者CSD内的页/page，并不是指的闪存中擦除单位的块/block。通常块是4KB大小，在Cosmos中一个page、即文中的块为16KB，因此在主机中提到块时指的是4KB大小的块，在固件中提到块时是16KB的块，他们在FTL中需要进行转换。

### 二.架构介绍

近数据计算（NDP，Near-data processing）作为一种新兴的计算范式正在蓬勃发展之中。NDP主要解决的问题是传统存储系统的存储墙问题，相比于传统系统中需要将数据全部从storage搬运到dram、再搬运到CPU中进行计算，NDP系统可以在存储器内部直接进行计算后返回计算结果。NDP极大地减轻了通信的开销以及主机CPU的负担，具有性能易扩展、能耗低、性能高等特点。

能够支持近数据计算功能的存储器称为计算型存储（CSD，Computational Storage）。由于闪存和SSD的性能进步以及成本的降低，CSD通常由SSD扩展而来。CSD的固件通常是闭源的（例如三星的SmartSSD、NGD system的Newport等），非常不利于NDP技术的发展。即使已经有开源的NDP系统（例如ATC‘19的Insider、FAST’22的FusionFS等），他们也往往并不是真的在SSD设备上进行的实现，而是采用FPGA模拟或者在主机端进行模拟，无法真实应用。因此本框架的目的是打破这一困境，基于开放闪存盘Cosmos Plus OpenSSD开发了一个简易的NDP开发框架easyNDP，便于开发者和研究人员更方便的开发NDP应用和进行NDP系统的研究。

easyNDP包含2部分的代码，分别是主机端和固件端。主机端部分主要负责为应用提供接口，同时通过NVMe协议与Cosmos进行交互，分发任务以及读写数据。

easyNDP的基本原理是，通过对数据从闪存中读取完成的处理函数进行拦截，执行一段数据处理程序以实现NDP操作。具体流程是如下：

1. 主机端应用先在内存中构建出需要下发的自定义指令结构和数据结构，然后通过API下发NDP请求，指令的阻塞与否取决于固件的程序。
2. 固件在接受到请求后，首先可以通过DMA接收刚刚在主机内存中构建的指令然后解析出所需的信息（如数据地址、操作类型、操作参数等），然后将请求推送到NDP任务队列中，固件继续接受新的请求。NDP任务队列是easyNDP新增的数据结构，主要作用是维护NDP相关的任务，例如计算、分发闪存操作、聚合计算结果等。
3. 在固件的每次主循环末尾，都会检查NDP任务队列是否有尚未完成的请求，若有则遍历NDP任务队列查找是否有已经完成的请求（开销比较大，后续可以进行优化例如专门设定一个待完成任务队列或者取消这个遍历过程采用触发式的完成）。若某个请求已完成则返回结果并删除该任务，若某请求处于初始化状态则进行任务分发。
4. easyNDP在闪存的读取完成函数中埋了一个处理函数，每当读上来一个数据就进行一次处理，目前只支持当场计算模式，处理完成后设置任务的完成状态以便于在步骤3中进行请求的处理。

### 三.应用开发及使用示例

这里采用一个在文本文件内进行字符串检索的应用作为示例。这里采用文件系统对数据进行管理，CSD作为一个块设备在主机端进行挂载并且可以进行文件读写操作。这里采用从上到下，即从应用到底层的顺序进行示例介绍。

#### 1. 主机端（代码在Host文件夹中）

1. Cosmos首先通过创建或者拷贝的方式往Cosmos中写入一个文本文件，接着用`fsync`同步元数据。由于CSD中包含DRAM数据缓存，写入的文件是先保存在缓存中没有写入闪存中的，并且Cosmos的固件（FTL）中没有支持刷写缓存的操作，我们需要写入一个数十MB的文件（Cosmos数据缓存默认大小是16MB）来保证文件已经全部写入到了闪存。

2. 文件写入后需要获取该文件的逻辑块地址。因为CSD中并没有文件语义的信息，无法通过文件名来获取文件存储的位置，因此需要主机告诉CSD数据所在的块。这里的逻辑块地址与物理块地址是针对CSD中来说的，主机与CSD交互时利用的是逻辑块地址，CSD中的FTL负责将逻辑块地址转换为物理块地址。获取块地址有现成的应用叫`filefrag`，可以通过该应用获取某个文件的起始块地址、文件长度的信息，指令格式为`filefrag -v -b4096 <file name>`。

3. 获取到文件地址后我们将其作为参数填入到内存中。本步骤的代码包含在`string_demo.c`中。我们首先需要包含`dma.h`头文件，该文件包含了与Cosmos进行交互的接口和底层代码，基于unvme实现。接着我们申请一块内存区域，用于按顺序存放我们所需的字段。例如步骤2中我们获取到的文件起始逻辑块号为33793，长度为4000B，占用1个块，那么我们可以按如下方法构建出指令：![image-20230217205445932](https://gitee.com/lijiali1101/picbed/raw/master/image/202302172054001.png)

4. 构建出NDP指令后下一步是发送。发送前需要先打开unvme设备，发送后需要关闭，NVMe设备路径在`dma.h`中修改，不过一般不会改变都是`/dev/nvme0n1`。发送的API是`DMA_Send`，第一个参数目前没有用；第二个参数是刚刚构建的指令地址；第三个参数是指令长度；第四个是指令操作符由应用自行定义，建议操作符定义专门作为一个头文件，方便在主机端和固件端进行同步，例如本例中在`command_list.h`中定义了多种操作，固件端与主机端头文件内容完全相同。![image-20230217210733398](https://gitee.com/lijiali1101/picbed/raw/master/image/202302172107423.png)

5. 至此，主机端的代码开发完成，进行编译即可。

#### 2. 固件端（代码在CSD文件夹中）

1. 固件端我们首先需要解析刚刚收到的请求。NVMe的通信原理是首先通过数十个Byte的NVMe指令发送基础信息给NVMe设备，然后所需的数据再通过DMA的方式（DMA地址在NVMe指令中）接受或者发送。我们刚刚在主机端第4步定义的`TEST_C`请求类型在这里就发挥作用了。请求类型保存在NVMe指令的`writeInfo12.reserved0`保留字段中，相关代码在`nvme_io_cmd.c`的第120行。程序解析到是`TEST_C`请求后，首先通过DMA接受请求数据，即刚刚在主机端第3步构建的请求数据。第124行是发起dma传输，第125行的作用是阻塞等待dma传输完成（可以设计成非阻塞的，会复杂很多因为CSD内部没有多进程机制需要自己手动进行完成状态检查，我们这里先用阻塞接受的方式）。![image-20230217211457777](https://gitee.com/lijiali1101/picbed/raw/master/image/202302172114805.png)
2. 在接受到请求后我们可以按序解析出所需的请求字段，这部分没太多重点。不过我们可以设置一个检查机制，简单检查一下数据是否有错误。例如我们这里的块数量信息和数据的字节大小其实是存在冗余的，因此可以通过计算他们的值是否匹配来检查数据是否有错误。我们也可以在字段里面构建校验码、幻数或者序号来检查。![image-20230217211445414](https://gitee.com/lijiali1101/picbed/raw/master/image/202302172114439.png)

3. 接着我们将这个请求推入到任务列表`tasklist`中。首先构建一个任务实体`task_entry`并全部置0（`task_entry`设计的是所有字段都可以置0以表示无效状态）。同时将所需的字段信息填入到`task_entry`的相关项中。注意状态项设置为`INIT`表示这个指令刚刚被推入任务列表中等待进一步被处理，请求槽`cmdSlotTag`是用于标识这个请求所属的NVMe请求的，用于完成请求返回时调用。注意插入操作可能会失败，因为任务列表可能已满，可以设置一个内存空间用于保存这些暂时无法被送入的请求，也可以像示例一样直接返回请求。`set_auto_nvme_cpl`的作用就是将这一NVMe请求设置为完成，调用时主机那边就会解除阻塞，在调用前主机那边的程序一直都是阻塞状态。

![image-20230217211930669](https://gitee.com/lijiali1101/picbed/raw/master/image/202302172119693.png)

4. 解析NVMe请求完成后，固件就会进入到任务列表的处理函数`check_task_list`中，该函数在`task_handler.c`的第199行。一个新应用需要做的就是在205行的for循环内新增一个任务处理程序。例如以`TEST`任务为例，我们首先检查他的状态，如果是初始化状态那么我们就分发他需要读取的块地址的读请求，这部分可以直接使用已经写好的函数`generate_child_task`，这个函数会直接分发写入在`task_entry`结构中的块地址请求信息，并且在分发完成后直接置其为`READING`状态。`generate_child_task`函数会根据请求数据长度自动分割为多个`FLASHREAD`指令并增加父任务（在这里就是`TEST`任务）的子任务`child_task_count`的数量，一个`FLASHREAD`指令负责一个16K块的数据读取。其次，当指令变成`READING`状态后，我们需要检查他的子任务是否都已经完成，子任务的数量控制我们在第5步中描述。如果完成的子任务数大于等于（正常情况下不会大于只会等于，但是我们在开发阶段加上大于可以避免某些bug），那么就说明该任务已经完成，可以设置NVMe指令完成并且利用`reset_task`重置本任务以便接受下一个任务了。数据的返回也是利用DMA完成，该功能待补充。

   ![image-20230217213001529](https://gitee.com/lijiali1101/picbed/raw/master/image/202302172130558.png)

5.最后一步就是在`handle_task`(`task_handler.c`第292行)添加数据处理函数了。在Cosmos的固件中，每当一个读闪存请求完成后，都会调用`low_level_scheduler.c`第694行的内容，我们在里面插入了一个`handle_task`函数，当一个块的数据读上来后我们会检查这是否是一个NDP所发出的读请求（通过FTL请求的`task_index`字段是否有值判断），是的话我们就会调用`handle_task`函数，把数据在DRAM中的缓存地址、任务编号、数据偏移量等信息传递到数据处理函数中。如图所示，当所完成的读任务的编号是我们的`FLASHREAD`任务时，我们进一步检查这个读任务的父任务是否是我们的`TEST`任务，是的话我们将其打印出来（也可以通过一个字符串检索函数检索关键字，这里只是简单的打印处理），并且将父任务即`TEST`的已完成子请求数量+1。那么在下一次进行任务列表遍历时就可以发现子任务已完成就可以返回了，如第四步所示。

![image-20230217215051417](https://gitee.com/lijiali1101/picbed/raw/master/image/202302172150442.png)

### 四.最后

easyNDP目前还很不完善，还存在很多bug，例如当任务列表已满时会执行出错。以及许多功能尚未实现例如DMA返回结果，例如在盘内写入数据，例如检查CSD的数据缓存是否存在所需数据。希望能获得大家的完善和支持。