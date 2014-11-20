#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/dcache.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/backing-dev.h>
#include <linux/ramfs.h>
#include <linux/string.h>
#include <linux/pagevec.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/time.h>
#include <linux/slab.h>
#define MRFS_MAGIC 0x6d726673 	//定义魔数
#define MRFS_DEFAULT_MODE 0777	//定义权限
static const struct super_operations mrfs_pops;
static const struct inode_operations ramfs_dir_inode_operations;
/* 
 * For address_spaces which do not use buffers nor write back. 
 */ 
int __set_page_dirty_no_writeback(struct page *page)
{
         if (!PageDirty(page))
                 return !TestSetPageDirty(page);
         return 0;
}
static const struct address_space_operations mrfs_aops = {//"地址空间"结构，将缓存数据与其来源之间建立关联
	.readpage	= simple_readpage,
	.write_begin	= simple_write_begin,
	.write_end	= simple_write_end,
	.set_page_dirty	= __set_page_dirty_no_writeback,
};
static struct backing_dev_info mrfs_backing_dev_info = {//该结构描述了作为数据来源和去向的后备存储设备的相关描述信息
	.name		= "mrfs",
	.ra_pages	= 0,	
	.capabilities	= BDI_CAP_NO_ACCT_AND_WRITEBACK |
			  BDI_CAP_MAP_DIRECT | BDI_CAP_MAP_COPY |
			  BDI_CAP_READ_MAP | BDI_CAP_WRITE_MAP | BDI_CAP_EXEC_MAP,
};
const struct file_operations ramfs_file_operations = {//该结构为ramfs中通用文件操作函数集合
	.read		= do_sync_read,
	.aio_read	= generic_file_aio_read,
	.write		= do_sync_write,
	.aio_write	= generic_file_aio_write,
	.mmap		= generic_file_mmap,
	.fsync		= noop_fsync,
	.splice_read	= generic_file_splice_read,
	.splice_write	= generic_file_splice_write,
	.llseek		= generic_file_llseek,
};

const struct inode_operations ramfs_file_inode_operations = {//包括ramfs的文件节点操作函数集合
	.setattr	= simple_setattr,
	.getattr	= simple_getattr,
};
struct inode *mrfs_get_inode(struct super_block *sb,
				const struct inode *dir, umode_t mode, dev_t dev)
{
	struct inode *inode = new_inode(sb);//分配一个结点
	mode |= MRFS_DEFAULT_MODE;	//设置初始权限
	if(inode){			//初始结点
		inode->i_ino = get_next_ino();	
		inode_init_owner(inode,dir,mode);//根据posix标准，为新的结点初始uid，gid模式
		inode->i_mapping->a_ops = &mrfs_aops;
		inode->i_mapping->backing_dev_info = &mrfs_backing_dev_info;
		mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
		mapping_set_unevictable(inode->i_mapping);
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {//设置结点和文件操作
		default:		//特殊文件的操作
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:		//普通文件的操作
			inode->i_op = &ramfs_file_inode_operations;
			inode->i_fop = &ramfs_file_operations;
			break;
		case S_IFDIR:		//文件夹的操作
			inode->i_op = &ramfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;
			inc_nlink(inode);
			break;
		}	
	}
	return inode;
}
static int
ramfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode * inode = mrfs_get_inode(dir->i_sb, dir, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
		dir->i_mtime = dir->i_ctime = CURRENT_TIME;
	}
	return error;
}

static int ramfs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
	int retval = ramfs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int ramfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	return ramfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int ramfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = mrfs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
			dir->i_mtime = dir->i_ctime = CURRENT_TIME;
		} else
			iput(inode);
	}
	return error;
}


static const struct inode_operations ramfs_dir_inode_operations = {	//文件夹节点的操作
	.create		= ramfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= ramfs_symlink,
	.mkdir		= ramfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= ramfs_mknod,
	.rename		= simple_rename,
};
static const struct super_operations mrfs_pops = {	//超级块的操作
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.show_options	= generic_show_options,
};
struct ramfs_mount_opts {
	umode_t mode;
};

enum {
	Opt_mode,
	Opt_err
};

static const match_table_t tokens = {
	{Opt_mode, "mode=%o"},
	{Opt_err, NULL}
};

struct ramfs_fs_info {
	struct ramfs_mount_opts mount_opts;
};


int mrfs_fill_super(struct super_block *sb, void *data, int silent)//填充超级块
{
	struct inode *inode;
	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_CACHE_SIZE;
	sb->s_blocksize_bits	= PAGE_CACHE_SHIFT;
	sb->s_magic		= MRFS_MAGIC;		//定义文件系统的魔数
	sb->s_op		= &mrfs_pops;		//指向一个超级块操作集super_operations
	sb->s_time_gran		= 1;	
	inode = mrfs_get_inode(sb, NULL, S_IFDIR, 0);
	sb->s_root = d_make_root(inode);			
	if (!sb->s_root)
		return -ENOMEM;
	return 0;
}
struct dentry *mrfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)		//挂载文件系统
{
	return mount_nodev(fs_type, flags, data, mrfs_fill_super);
}
static void mrfs_kill_sb(struct super_block *sb)	//删除超级块
{
	kfree(sb->s_fs_info);
	kill_litter_super(sb);
}
static struct file_system_type mrfs_type = {
	.owner 		= THIS_MODULE,
	.name		= "mrfs",	//定义文件系统的名字
	.mount		= mrfs_mount,
	.kill_sb	= mrfs_kill_sb,
	.fs_flags	= FS_USERNS_MOUNT,
};
static int __init init_procfs(void)
{
	register_filesystem(&mrfs_type);//注册文件系统
	printk("hello kernel\n");
	return 0;
}
static void __exit exit_procfs(void)
{
	unregister_filesystem(&mrfs_type);//注销文件系统	
	printk("GoodBye kernel\n");
}
module_init(init_procfs);
module_exit(exit_procfs);
MODULE_LICENSE("GPL");


