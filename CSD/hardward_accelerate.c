#include "hardware_accelerate.h"
#include "memory_map.h"


unsigned int startLBA = 0x1000;

void init_metadata(){
	ckpt.checkpoint_ver = 0;
}

// used to update super block info
void updateSB(unsigned int dataAddr)
{
	struct f2fs_super_block *SB_origin = (struct f2fs_super_block *)(dataAddr + 0x400);
	sb.log_blocks_per_seg = SB_origin->log_blocks_per_seg;
	sb.cp_blkaddr = SB_origin->cp_blkaddr;
	sb.nat_blkaddr = SB_origin->nat_blkaddr;
	sb.root_ino = SB_origin->root_ino;
	sb.cp_payload = SB_origin->cp_payload;

#ifdef DEBUG
	printf("[updateSB] Super Block update sucessfully.\r\n");
#endif
}

// used to update checkpoint info
void updateCP(unsigned int dataAddr)
{
	struct f2fs_checkpoint *temp_ckpt = (struct f2fs_checkpoint *)dataAddr;
	if (temp_ckpt->checkpoint_ver == 0)
		return;
	else if (temp_ckpt->checkpoint_ver > ckpt.checkpoint_ver)
	{  
		// update ckpt
		ckpt.checkpoint_ver = temp_ckpt->checkpoint_ver;
		ckpt.ckpt_flags = temp_ckpt->ckpt_flags;
		ckpt.sit_ver_bitmap_bytesize = temp_ckpt->sit_ver_bitmap_bytesize;
		ckpt.nat_ver_bitmap_bytesize = temp_ckpt->nat_ver_bitmap_bytesize;

		// XTime t_start, t_end;
		// XTime_GetTime(&t_start);
		memcpy(&ckpt.sit_nat_version_bitmap, &temp_ckpt->sit_nat_version_bitmap, ckpt.sit_ver_bitmap_bytesize + ckpt.nat_ver_bitmap_bytesize);
		// XTime_GetTime(&t_end);
		// t_memory_copy += t_end - t_start;

	// printf("[updateCP] new CKPT version: %llx\r\n", ckpt.checkpoint_ver);

		// update nat journal
		if (temp_ckpt->cp_pack_start_sum > 3) {
			printf("[updateCP] Warning: cp_pack_start_sum > 3 !!!\r\n");		
			ckpt.checkpoint_ver = 0;	
		}
		else{
			struct f2fs_summary_block *sum_block = (struct f2fs_summary_block *)(dataAddr + temp_ckpt->cp_pack_start_sum * 4096);

			if (sum_block->n_nats == 0)  // if no nat_journals in sum block, it is not necessary to memcpy
				sum.n_nats = 0;
			else{
				// XTime_GetTime(&t_start);
				memcpy(&sum, sum_block, sizeof(struct f2fs_summary_block));
				// XTime_GetTime(&t_end);
				// t_memory_copy += t_end - t_start;		
			}
		}
// #ifdef DEBUG
// 	printf("[updateCP] n_nats in summary block: %x\r\n", sum.n_nats);
// #endif
	}
	else if (temp_ckpt->checkpoint_ver == ckpt.checkpoint_ver)
	{  
		// only update the nat journal
		if (temp_ckpt->cp_pack_start_sum > 3)
			printf("[updateCP] Warning: cp_pack_start_sum > 3 !!!\r\n");
		else{
			struct f2fs_summary_block *sum_block = (struct f2fs_summary_block *)(dataAddr + temp_ckpt->cp_pack_start_sum * 4096);

			if (sum_block->n_nats != sum.n_nats)  // if no nat_journals in sum block, it is not necessary to memcpy
			{
				// XTime t_start, t_end;
				// XTime_GetTime(&t_start);
				memcpy(&sum, sum_block, sizeof(struct f2fs_summary_block));
				// XTime_GetTime(&t_end);
				// t_memory_copy += t_end - t_start;		
			}
		}
		printf("[updateCP] n_nats in summary block: %x\r\n", sum.n_nats);
	}
	else {
		printf("[updateCP] received version: %llx < cached version: %llx, update failed.\r\n", temp_ckpt->checkpoint_ver, ckpt.checkpoint_ver);
	}
}

/*
	This is used to calculate the lba of NAT block of nid;
*/
inline int f2fs_test_bit(unsigned int nr, char *addr)
{
	int mask;

	addr += (nr >> 3);
	mask = 1 << (7 - (nr & 0x07));
	return mask & *addr;
}
unsigned int getNidNATLba(int nid, struct f2fs_super_block *sb, struct f2fs_checkpoint *ckpt)
{
	// 1.get bitmap
	//**************this code may have some questions***********************
	__le32 nat_bitmap_bytesize = ckpt->nat_ver_bitmap_bytesize;
	char *version_nat_bitmap;
	int offset;

	if (ckpt->ckpt_flags & CP_LARGE_NAT_BITMAP_FLAG){
		offset = 0;
		version_nat_bitmap = &ckpt->sit_nat_version_bitmap + offset + sizeof(__le32);
	}
	else if (sb->cp_payload > 0){
		version_nat_bitmap = &ckpt->sit_nat_version_bitmap;
	}
	else{
		// actually it will execute this ...
		offset = ckpt->sit_ver_bitmap_bytesize;
		version_nat_bitmap = &ckpt->sit_nat_version_bitmap + offset;
	}

	char *nat_bitmap = version_nat_bitmap;

	// 2.get the current nat block page
	int block_off;
	int block_addr;
	block_off = NAT_BLOCK_OFFSET(nid);
	unsigned int blocks_per_seg = 1 << sb->log_blocks_per_seg;
	block_addr = (int)(sb->nat_blkaddr + (block_off << 1) - (block_off & (blocks_per_seg - 1)));
	if (f2fs_test_bit(block_off, nat_bitmap)){
		block_addr += blocks_per_seg;
	}
	// get lba of nid
	// 	printf("block_addr:%x\r\n",block_addr);
	return block_addr;
}
inline unsigned int getNidLba(int nid, unsigned int block_addr)
{
	unsigned int dramAddrNat = handle_dram_flash_read(block_addr / 4);
	// struct f2fs_nat_block nat_block_addr; // = malloc(sizeof(struct f2fs_nat_block));
	// memcpy(&nat_block_addr, dramAddrNat + block_addr % 4 * 4096, sizeof(struct f2fs_nat_block));

	unsigned int entry_offset = nid - START_NID(nid);  // 相较于第一条entry的偏移
	unsigned int start_addr = dramAddrNat + block_addr % 4 * 4096 + entry_offset * sizeof(struct f2fs_nat_entry);  // 目标entry的所在地址
	unsigned int aligned_start_addr = start_addr & 0xfffffffc;  // 得到对齐后的地址
	unsigned int buffer_size = start_addr - aligned_start_addr + sizeof(struct f2fs_nat_entry);
	char buffer[buffer_size];
	memcpy(buffer, aligned_start_addr, buffer_size);
	struct f2fs_nat_entry *nat_entry = (struct f2fs_nat_entry*)(start_addr - aligned_start_addr + buffer);

	// unsigned int return_block_addr = nat_entry->block_addr;
	return nat_entry->block_addr;
}
inline __le32 read_NAT(int nid, struct f2fs_super_block *sb, struct f2fs_checkpoint *ckpt)
{
	unsigned int block_addr = getNidNATLba(nid, sb, ckpt);
	return getNidLba(nid, block_addr + startLBA);
}

unsigned int search_keyword()
{
	//--------------------0.get information from the dram buffer------------------------------
	unsigned int devAddr = FS_FOPEN_PATH_ADDR;
	// startLBA = IO_READ32(devAddr);
	devAddr = devAddr + sizeof(unsigned int);

	unsigned int path_len = *((unsigned int *)devAddr);
	if (!path_len)
		return 0;

	char path[path_len]; //[filefindlen+1];
	memcpy(path, devAddr + sizeof(unsigned int), path_len);
	path[path_len] = '\0';
	//--------------------2.get file's ino ------------------------------
	unsigned int fileino = f2fs_path(path, path_len);

#ifdef DEBUG
	printf("[search_keyword] the final fileino: %d\n", fileino);
#endif
	return fileino;
}

void retrieve_address(unsigned int ino, unsigned int *blk_addr, unsigned int *blk_num){
	unsigned int inode_pbn = 0;
	
	for (int i = 0; i < sum.n_nats; i++)
	{
		if (sum.nat_j.entries[i].nid == ino)
		{
			inode_pbn = sum.nat_j.entries[i].ne.block_addr;
			break;
		}
	}
	
	if (inode_pbn == 0)
		inode_pbn = read_NAT(ino, &sb, &ckpt);
		
	unsigned int data_page_addr = handle_dram_flash_read((inode_pbn + 4096) / 4);
	struct f2fs_inode *inode = (struct f2fs_inode *)(data_page_addr + (inode_pbn % 4) * 4096);

	*blk_addr = inode->i_addr[0] + 4096;
	unsigned int i_size = inode->i_size & 0xffffffff;
	*blk_num = (i_size + 4095) / 4096;

#ifdef DEBUG
	xil_printf("[retrieve_address] ino %d , blk_addr: %d , blk_num: %d\r\n", ino, *blk_addr, *blk_num);
#endif
}

void read_inode(unsigned int ino){
	unsigned int inode_pbn = 0;
	
	for (int i = 0; i < sum.n_nats; i++)
	{
		if (sum.nat_j.entries[i].nid == ino)
		{
			inode_pbn = sum.nat_j.entries[i].ne.block_addr;
			break;
		}
	}
#ifdef TIME_COUNTER
	XTime t_start, t_end;
	XTime_GetTime(&t_start);
#endif
	if (inode_pbn == 0)
		inode_pbn = read_NAT(ino, &sb, &ckpt);
#ifdef TIME_COUNTER
	XTime_GetTime(&t_end);
	t_read_nat += t_end - t_start;
#endif

#ifdef TIME_COUNTER
	XTime_GetTime(&t_start);
#endif
	unsigned int data_page_addr = handle_dram_flash_read((inode_pbn + 4096) / 4);
#ifdef TIME_COUNTER
	XTime_GetTime(&t_end);
	t_read_block += t_end - t_start;
#endif
}

// for hash
static void TEA_transform(unsigned int buf[4], unsigned int const in[])
{
	__u32 sum = 0;
	__u32 b0 = buf[0], b1 = buf[1];
	__u32 a = in[0], b = in[1], c = in[2], d = in[3];
	int n = 16;

	do
	{
		sum += DELTA;
		b0 += ((b1 << 4) + a) ^ (b1 + sum) ^ ((b1 >> 5) + b);
		b1 += ((b0 << 4) + c) ^ (b0 + sum) ^ ((b0 >> 5) + d);
	} while (--n);

	buf[0] += b0;
	buf[1] += b1;
}

static void str2hashbuf(const unsigned char *msg, size_t len, unsigned int *buf, int num)
{
	unsigned pad, val;
	int i;

	pad = (__u32)len | ((__u32)len << 8);
	pad |= pad << 16;

	val = pad;
	if (len > num * 4)
		len = num * 4;
	for (i = 0; i < len; i++)
	{
		if ((i % 4) == 0)
			val = pad;
		val = msg[i] + (val << 8);
		if ((i % 4) == 3)
		{
			*buf++ = val;
			val = pad;
			num--;
		}
	}
	if (--num >= 0)
		*buf++ = val;
	while (--num >= 0)
		*buf++ = pad;
}
f2fs_hash_t path_hash(char *name, __le16 len1)
{
	__u32 hash;
	f2fs_hash_t f2fs_hash;
	const unsigned char *p;
	__u32 in[8], buf[4];
	size_t len = len1;

	if (is_dot_dotdot(name, len1))
		return 0;

	buf[0] = 0x67452301;
	buf[1] = 0xefcdab89;
	buf[2] = 0x98badcfe;
	buf[3] = 0x10325476;

	p = name;
	while (1)
	{
		str2hashbuf(p, len, in, 4);
		TEA_transform(buf, in);
		p += 16;
		if (len <= 16)
			break;
		len -= 16;
	}
	hash = buf[0];
	f2fs_hash = cpu_to_le32(hash & ~F2FS_HASH_COL_BIT);

	return f2fs_hash;
}
inline int is_dot_dotdot(char *str, __le16 len)
{
	if (len == 1 && str[0] == '.')
		return 1;

	if (len == 2 && str[0] == '.' && str[1] == '.')
		return 1;

	return 0;
}

// 从path字符串中提取下一级dir
int extract_dir(const char* path, const unsigned int path_len, unsigned int *dir_offset, unsigned int *dir_len)
{
	if (path[*dir_offset + *dir_len] == '\0')  // 已经到末级了，不用往下走了
		return 0;
	
	unsigned int offset = *dir_offset + *dir_len + 1;
	unsigned int len = 0;
	for (unsigned int i = offset; i < path_len; i++, len++){
		if (path[i] == '/')
			break;
	}

	*dir_offset = offset;
	*dir_len = len;
	return 1;
}

/*receive path of file,return the number of inode of this file*/
__le32 f2fs_path(char *path, unsigned int path_len)
{
#ifdef DEBUG
	printf("[f2fs_path] target path is:%s\r\n", path);
#endif
#ifdef TIME_TAGS
	strcpy(time_tags.tags[time_tags.index].tag, "[f2fs_path]start: ");
	XTime_GetTime(&(time_tags.tags[time_tags.index++].time));
#endif
	//判断传入的路径是否只有根目录，若是，返回根目录的inode number
	if ((path[0] == '/') && (path[1] == '\0'))
	{
		printf("[f2fs_path]: target path is root.\n");
		return sb.root_ino;
	}

	__le32 par_ino = sb.root_ino;
	__le32 file_ino = 0;
	unsigned int dir_index = 0, dir_len = 0;

	while (extract_dir(path, path_len, &dir_index, &dir_len))
	{
		// 2. 根据parent的inode number和dir名称，在parent_dir中查找dir，找到返回dir的inode number
		file_ino = find_dir(&sb, &ckpt, par_ino, path + dir_index, dir_len);
		if (file_ino == 0)
		{
			xil_printf("[f2fs_path] !!! find dir failed !!!\n");
			return 0;
		}
		// 3. 现在的dir成为了parent
		par_ino = file_ino;
	}

#ifdef DEBUG
	printf("[f2fs_path] the final fileino: %d\n", file_ino);
#endif

	return file_ino;
}

__le32 find_dir(struct f2fs_super_block *sb, struct f2fs_checkpoint *ckpt, __le32 par_ino, char *dir, unsigned int dir_len)
{
	// printf("---------------enter in find_dir--------------------\n");
	// next_ino saves the inode number of dir,if not find,return -1
	if (par_ino == 0)
		return 0;

#ifdef TIME_TAGS
	strcpy(time_tags.tags[time_tags.index].tag, "[find_dir]start: ");
	XTime_GetTime(&(time_tags.tags[time_tags.index++].time));
#endif

	__le32 next_ino = 0;
	__le32 par_blkaddr = 0;
	// read from journal
#ifdef TIME_COUNTER
	XTime t_start, t_end;
	XTime_GetTime(&t_start);
#endif
	for (int i = 0; i < sum.n_nats; i++)
	{
		if (sum.nat_j.entries[i].nid == par_ino)
		{
			par_blkaddr = sum.nat_j.entries[i].ne.block_addr;
			break;
		}
	}
	if (par_blkaddr == 0){ // not found in journal, then read nat
		// 1. 根据par_ino查找NAT,得到父目录的inode对应的inode块地址
		par_blkaddr = read_NAT(par_ino, sb, ckpt); // par_blkaddr logic address
#ifdef DEBUG
		printf("[read_NAT] parent inode is %d, NAT returned %d.\n", par_ino, par_blkaddr);
#endif
	}
#ifdef TIME_COUNTER
	XTime_GetTime(&t_end);
	t_read_nat += t_end - t_start;
#endif

	// 2. 根据NAT中的块地址得到parent inode block.$$$$$ judge if the bitmap valid.This is used to do later.
#ifdef TIME_COUNTER
	XTime_GetTime(&t_start);
#endif
	unsigned int dramAddrparent_inode = handle_dram_flash_read((par_blkaddr + startLBA) / 4);
#ifdef TIME_COUNTER
	XTime_GetTime(&t_end);
	t_read_block += t_end - t_start;
#endif

	struct f2fs_inode *par_inode = (struct f2fs_inode *)(dramAddrparent_inode + par_blkaddr % 4 * 4096);

// #ifdef TIME_TAGS
// 	strcpy(time_tags.tags[time_tags.index].tag, "[find_dir]read inode done: ");
// 	XTime_GetTime(&(time_tags.tags[time_tags.index++].time));
// #endif

	// 3. 遍历parent inode中的dentry block，按照目录名dir进行hash查找
	// 3.1 计算dir对应的hash值
#ifdef TIME_COUNTER
	XTime_GetTime(&t_start);
#endif
	f2fs_hash_t dir_hash = path_hash(dir, dir_len);
#ifdef TIME_COUNTER
	XTime_GetTime(&t_end);
	t_path_hash += t_end - t_start;
#endif
	// printf("hash code of dir is %x\n", dir_hash);

	// 3.2 遍历每一个不为空的dentry_block				 
	// __le64 i_blocks = parent_inode.i_blocks; // i_blocks 保存root inode 中不为0的i_addr的数目，包括inode block本身，所以应该-1
	__le64 i_blocks = par_inode->i_blocks;

	if (i_blocks == 1) //如果i_blocks=1，说明这个inode对应的文件数据以inline形式存储
	{
// #ifdef TIME_TAGS
// 	strcpy(time_tags.tags[time_tags.index].tag, "[find_dir]inline_dentry start: ");
// 	XTime_GetTime(&(time_tags.tags[time_tags.index++].time));
// #endif
		// next_ino = lookup_in_inline_inode(&parent_inode, dir, dir_hash);
		next_ino = lookup_in_inline_inode(par_inode, dir, dir_hash);
	}
	else{
		i_blocks = i_blocks > 20? 20:i_blocks;
		// unsigned int i_addrs[i_blocks];
		// memcpy(i_addrs, par_inode->i_addr, i_blocks * sizeof(__le32));

// #ifdef TIME_TAGS
// 	strcpy(time_tags.tags[time_tags.index].tag, "[find_dir]lookup_denblk start: ");
// 	XTime_GetTime(&(time_tags.tags[time_tags.index++].time));
// #endif

		for (int i = 0; i < i_blocks; i++)
		{
			next_ino = lookup_in_denblk(par_inode->i_addr[i], dir, dir_hash);
			if (next_ino != 0)
			{
				break;
			}
		}
	}

	return next_ino;
}


// for dentry
/*look up dir in one dentry block*/
__le32 lookup_in_denblk(__le32 denblk_in_root, char *dir, f2fs_hash_t dir_hash)
{
	//    printf("---------------enter in lookup_in_denblk---------------\r\n");
	__le32 next_ino = 0; //如果下一级的inode number找到，保存并返回

	// 1. read denblk_in_root address
#ifdef TIME_COUNTER
	XTime t_start, t_end;
	XTime_GetTime(&t_start);
#endif
	unsigned int dramAddrdenblk_in_root = handle_dram_flash_read((denblk_in_root + startLBA) / 4);
#ifdef TIME_COUNTER
	XTime_GetTime(&t_end);
	t_read_block += t_end - t_start;
#endif

	struct f2fs_dentry_block *par_den_blk = (struct f2fs_dentry_block *)(dramAddrdenblk_in_root + denblk_in_root % 4 * 4096);

// #ifdef TIME_TAGS
// 	strcpy(time_tags.tags[time_tags.index].tag, "[denblk]read denblk done: ");
// 	XTime_GetTime(&(time_tags.tags[time_tags.index++].time));
// #endif
	// 2. 计算f2fs_dentry_block中有效的dentry数量
	__u8 valid_dentry = 0;
#ifdef TIME_COUNTER
	XTime_GetTime(&t_start);
#endif
	for (int i = 0; i < SIZE_OF_DENTRY_BITMAP; i++)
	{
		// printf("bitymap is %X\n",par_den_blk.dentry_bitmap[i]);
		// printf("i:%d\n",i);
		if (par_den_blk->dentry_bitmap[i] == 0x00)
			break;
		else
		{
			valid_dentry = valid_dentry + count_valid_dentry(par_den_blk->dentry_bitmap[i]);
		}
	}
#ifdef TIME_COUNTER
	XTime_GetTime(&t_end);
	t_count_dentry += t_end - t_start;
#endif
#ifdef DEBUG
	printf("[lookup_in_denblk] valid dentry number is %X\n", valid_dentry);
#endif

	// struct f2fs_dir_entry dentries[valid_dentry];
	unsigned int buffer_size = 30 + valid_dentry * sizeof(struct f2fs_dir_entry);
	char dentries_buffer[buffer_size];

#ifdef TIME_COUNTER
	XTime_GetTime(&t_start);
#endif
	memcpy(dentries_buffer, par_den_blk, buffer_size);
#ifdef TIME_COUNTER
	XTime_GetTime(&t_end);
	t_memory_copy += t_end - t_start;
#endif

	struct f2fs_dir_entry *dentry = (struct f2fs_dir_entry *)(dentries_buffer + 30);

#ifdef TIME_COUNTER
	XTime_GetTime(&t_start);
#endif
	// 3 根据bitmap的数量遍历dir_entry
	for (int i = 0; i < valid_dentry; i++, dentry++)
	{
		//首先比较entry中的hash值
		if (dir_hash == dentry->hash_code)
		{
			//然后当hash值相等时比较dentry_block中保存的filename
			// printf("enter hash\n");
			__le16 name_len = dentry->name_len;
			if (name_len == 0)
			{
				printf("name len is 0\n");
				continue;
			}
			// printf("name_len is %X\n", name_len);
			int match = 1;
			for (int j = 0; j < name_len; j++)
			{
				// printf("dir[j] is %c,filename[i][j] is %c\n",dir[j],par_den_blk.filename[i][j]);
				if (dir[j] != par_den_blk->filename[i][j])
				{
					match = 0;
					break;
				}
			}
			if (match)
			{
				next_ino = dentry->ino;
				break;
			}
		}
	}
#ifdef TIME_COUNTER
	XTime_GetTime(&t_end);
	t_find_dentry += t_end - t_start;
#endif
// #ifdef TIME_TAGS
// 	strcpy(time_tags.tags[time_tags.index].tag, "[denblk]lookup_denblk done: ");
// 	XTime_GetTime(&(time_tags.tags[time_tags.index++].time));
// #endif

	return next_ino;
}

__le32 lookup_in_inline_inode(struct f2fs_inode *parent_inode, char *dir, f2fs_hash_t dir_hash)
{
	//    printf("---------------enter in lookup_in_inline_inode---------------\r\n");

	__le32 next_ino = 0; //如果下一级的inode number找到，保存并返回

	//在inline情况下，从i_addr[1]开始才是对应的dentryblk的内容
	// i_addr[1]中保存的是bitmap
	__u8 valid_dentry = 0;
	char *inline_den = (char *)parent_inode + 16 * 22 + 12; // inline_den将inode中的内容视为字节文件，开始时指向i_addr[1]的位置
	
#ifdef TIME_COUNTER
	XTime t_start, t_end;
	XTime_GetTime(&t_start);
#endif
	for (int i = 0; i < SIZE_OF_DENTRY_BITMAP; i++)
	{
		if (*(inline_den + i) == 0x00)
			break;
		else
		{
			valid_dentry = valid_dentry + count_valid_dentry(*(inline_den + i));
		}
	}
#ifdef TIME_COUNTER
	XTime_GetTime(&t_end);
	t_count_dentry += t_end - t_start;
#endif
#ifdef DEBUG
	printf("[lookup_in_inline_inode] valid_entry in inline is %d\n",valid_dentry);
#endif

	//从i_addr[1]数30个字节跳过bitmap和保留字,到达第一个dir_entry
	inline_den = inline_den + 30;

	unsigned int aligned_addr_start = (unsigned int)inline_den & 0xfffffffc;
	unsigned int addr_end = inline_den + valid_dentry * sizeof(struct f2fs_dir_entry) - 1;
	unsigned int buffer_size = addr_end - aligned_addr_start + 1;
	char entries_buffer[buffer_size];

#ifdef TIME_COUNTER
	XTime_GetTime(&t_start);
#endif
	memcpy(entries_buffer, aligned_addr_start, buffer_size);
#ifdef TIME_COUNTER
	XTime_GetTime(&t_end);
	t_memory_copy += t_end - t_start;
#endif

	struct f2fs_dir_entry *entry = (struct f2fs_dir_entry *)((unsigned int)inline_den - aligned_addr_start + entries_buffer);
	// printf("entry: %x\n", entry);
	// struct f2fs_dir_entry entry; //=(struct f2fs_dir_entry*)malloc(sizeof(struct f2fs_dir_entry));
	// char *begin = inline_den;

#ifdef TIME_COUNTER
	XTime_GetTime(&t_start);
#endif
	for (int i = 0; i < valid_dentry; i++, entry++)
	{
		//当hash一样时比较文件名
		if (entry->hash_code == dir_hash)
		{
			__le16 name_len = entry->name_len;
			if (name_len == 0)
				continue;
			// printf("name_len is %X\n",name_len);

			int match = 1;
			int offset = 125 * 16 + 2;
			char *fname = inline_den + offset;
			for (int j = 0; j < name_len; j++)
			{
				fname = inline_den + offset + i * F2FS_SLOT_LEN + j;
				// printf("fname is %X:%X\n",fname,*fname);
				if (dir[j] != *fname)
				{
					match = 0;
					break;
				}
			}
			if (match)
			{
				next_ino = entry->ino;
				break;
			}
		}
	}
#ifdef TIME_COUNTER
	XTime_GetTime(&t_end);
	t_find_dentry += t_end - t_start;
#endif

	return next_ino;
}
unsigned char count_valid_dentry(unsigned char para)
{
	if (para & 0x01)
		para = para;
	if (para & 0x02)
		para = (para & 0xfd) + 1;
	if (para & 0x04)
		para = (para & 0xfb) + 1;
	if (para & 0x08)
		para = (para & 0xf7) + 1;
	if (para & 0x10)
		para = (para & 0xef) + 1;
	if (para & 0x20)
		para = (para & 0xdf) + 1;
	if (para & 0x40)
		para = (para & 0xbf) + 1;
	if (para & 0x80)
		para = (para & 0x7f) + 1;
	return para;
}
