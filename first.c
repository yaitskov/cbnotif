#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/syscalls.h>

#define DRIVER_AUTHOR "Daneel S. Yaitskov <rtfm.rtfm.rtfm@gmail.com>"
#define DRIVER_DESC   "Changed file blocks notifier"
#define SUCCESS 0

static int open_device(struct inode *, struct file *);
static int release_device(struct inode *, struct file *);
static ssize_t read_device(struct file *, char *, size_t, loff_t *);
static ssize_t write_device(struct file *, const char *, size_t, loff_t *);

static ssize_t write_inode(struct file *, const char __user *, size_t, loff_t *);
static ssize_t aio_write_inode(struct kiocb *, const struct iovec *, unsigned long, loff_t);
static ssize_t sendpage_inode(struct file *, struct page *, int, size_t, loff_t *, int);
static ssize_t splice_write_inode(struct pipe_inode_info *, struct file *, loff_t *, size_t, unsigned int);  
static struct first_monitored_inode * get_mi_by_file(struct file *);
/**
 * serving process.
 */
struct first_monitoring_process {
  struct list_head   next_process;
  struct mutex       mp_mutex;   /** sync threads of the process */
  long               pid;
  struct list_head   monitored_inodes;
};
 
/**
 * a file monitored by a process
 */
struct first_monitored_inode {
  struct list_head   next_inode;
  struct inode *     inode;  // inode of monitored file
  struct mutex       mi_mutex; // modification the file from multiple processes
  /**
   * Divides a file by blocks of fixed size.
   * If any byte of the block is changed then the block is treated as dirty.
   */
  int                block_size;
  int                num_dblocks;  /* number dirty blocks; if -1 then overflow */
  /**
   *  List dirty blocks about the monitoring process doesn't know yet.
   *  This list is cleaned after each monitoring process requies
   */   
  struct list_head   dblocks;
  /** original handlers */
  ssize_t (*orig_write) (struct file *, const char __user *, size_t, loff_t *);
  ssize_t (*orig_aio_write) (struct kiocb *, const struct iovec *, unsigned long, loff_t);
  ssize_t (*orig_sendpage) (struct file *, struct page *, int, size_t, loff_t *, int);
  ssize_t (*orig_splice_write)(struct pipe_inode_info *, struct file *, loff_t *, size_t, unsigned int);  
};

/**
 * continuous range of dirty blocks
 */
struct first_dirty_block {
  struct list_head    next;
  /**
   * the zero-based number of the first block in the continuous range. 
   */
  int                 first;
  /**
   * the size of the continuous range in blocks
   */
  int                 length;
};

//  module vars 
/**
 * sync mp_list structure modification
 */
static struct mutex mp_list_mutex;
/**
 * list of first_monitoring_process
 */
static struct list_head mp_list;

/**
 */
static struct file_operations file_device = {
  .read = read_device,
  .write = write_device,
  .open = open_device,
  .release = release_device
};

/**
 * the module params
 */
static unsigned int device_major = 0;
static char * device_name = "changed_blocks";

module_param(device_major, uint, 0000);
MODULE_PARM_DESC(device_major, " the major number of character device; 0 by default");
module_param(device_name, charp, 0000);
MODULE_PARM_DESC(device_name, " the character device name; 'changed_blocks' by default");

static int __init first_init(void) {
  int r;
  printk(KERN_INFO "first: start init\n");
  printk(KERN_INFO "first: page size: %ld\n", PAGE_SIZE);

  mutex_init(&mp_list_mutex);
  INIT_LIST_HEAD(&mp_list);
  r = register_chrdev(device_major, device_name, &file_device);
  if (r < 0) {
    printk(KERN_ALERT "first: register_chrdev failed with %d\n", r);
    return r;
  }
  if (!device_major)
    device_major = r;
  
  printk(KERN_INFO "first: end init device_major = %d\n", r);
  return 0;
}

static void __exit first_cleanup(void) {
  printk(KERN_INFO "first: cleanup with device_major = %d and device_name = %s\n",
         device_major, device_name);  
  unregister_chrdev(device_major, device_name);  
}

static int open_device(struct inode * inode, struct file * file) {
  struct list_head * mp;
  struct first_monitoring_process * _mp;
  struct task_struct * task;
  pid_t pid;
  task = get_current();
  pid = task->pid;
  printk(KERN_INFO "first: open_device inode = %p; file = %p; pid = %d\n", inode, file, pid);
  
  mutex_lock(&mp_list_mutex);
  list_for_each(mp, &mp_list) {
    _mp = (struct first_monitoring_process *)mp;
    if (_mp->pid == pid) {
      printk(KERN_INFO "first: process %d already opened this file driver\n", pid);
      break;
    }
  }

  if (&mp_list == mp) {
    // add new
    _mp = (struct first_monitoring_process *)kmalloc(sizeof(struct first_monitoring_process), GFP_KERNEL);
    if (!_mp) {
      printk(KERN_ALERT "first: no memory\n");
      mutex_unlock(&mp_list_mutex);
      return -ENOMEM;
    }
    mutex_init(&_mp->mp_mutex);
    _mp->pid = pid;
    INIT_LIST_HEAD(&_mp->next_process);
    try_module_get(THIS_MODULE);
    list_add((struct list_head*)_mp, &mp_list);
    printk(KERN_INFO "first: process %d successfully opened file\n", pid);    
  } else {
    printk(KERN_INFO "first: process %d already opened file\n", pid);        
  }

  mutex_unlock(&mp_list_mutex);
  return SUCCESS;
}

static int release_device(struct inode * inode, struct file * file) {
  struct list_head * mp, * mi;
  struct first_monitoring_process * _mp;
  struct first_monitored_inode    * _mi;
  struct file_operations * fops;  
  pid_t pid = get_current()->pid;
  printk(KERN_INFO "first: release_device inode = %p; file = %p; pid = %d\n", inode, file, pid);

  mutex_lock(&mp_list_mutex);
  if (list_empty(&mp_list)) {
    printk(KERN_WARNING "first: something wong - close closed file pid = %d\n", pid);
    mutex_unlock(&mp_list_mutex);
    return -EBADF;
  }

  list_for_each(mp, &mp_list) {
    _mp = (struct first_monitoring_process*)mp;
    if (_mp->pid == pid) {
      break;
    }
  }
  if (&mp_list == mp) {
    printk(KERN_WARNING "first: something wong - close closed file pid = %d\n", pid);    
    mutex_unlock(&mp_list_mutex);
    return -EBADF;
  }    
  list_del(mp);
  mutex_unlock(&mp_list_mutex);
  printk(KERN_INFO "first: process %d is removed from the list\n", pid);
  mutex_lock(&_mp->mp_mutex);
  // need to ensure that other threads of current process already gone.
  list_for_each(mi, &_mp->monitored_inodes) {
    _mi = (struct first_monitored_inode*)mi;
    if (mi->prev != &_mp->monitored_inodes) {
      kfree(mi->prev);
    }
    mutex_lock(&_mi->mi_mutex);
    mutex_lock(&_mi->inode->i_mutex);

    fops = (struct file_operations*)_mi->inode->i_fop;
    fops->write = _mi->orig_write;
    fops->aio_write = _mi->orig_aio_write;
    fops->sendpage = _mi->orig_sendpage;
    fops->splice_write = _mi->orig_splice_write;    
    
    mutex_unlock(&_mi->inode->i_mutex);    
    atomic_dec(&_mi->inode->i_count);
    mutex_unlock(&_mi->mi_mutex);
  }
  if (!list_empty(&_mp->monitored_inodes))
    kfree(mi->prev);  
  mutex_unlock(&_mp->mp_mutex);
  module_put(THIS_MODULE);
  kfree(mp);
  
  return SUCCESS;  
}
static ssize_t read_device(struct file * file, char * buf, size_t bufsize, loff_t * ofs) {
  printk(KERN_INFO "first: read_device file = %p; buf = %p; bufsize = %d\n", file, buf, bufsize);    
  return 0;  
}
static ssize_t write_device(struct file * file, const char * buf, size_t bufsize, loff_t * ofs) {
  printk(KERN_INFO "first: write_device file = %p\n", file);    
  return 0;  
}


static ssize_t write_inode(struct file * file, const char __user * data, size_t len, loff_t * ofs) {
  struct first_monitored_inode * mi = 0;
  printk(KERN_INFO "first: write_inode file = %p; len = %u\n", file, len);
  mi = get_mi_by_file(file);
  if (mi) {
    ssize_t (*writep) (struct file *, const char __user *, size_t, loff_t *);
    writep = mi->orig_write;
    //mutex_unlock(mi->inode->i_mutex);
    mutex_unlock(&mi->mi_mutex);
    return writep(file, data, len, ofs);    
  }
  return -ENXIO;
}
static ssize_t aio_write_inode(struct kiocb * kiocb, const struct iovec * iovec, unsigned long len, loff_t ofs) {
  struct first_monitored_inode * mi = 0;
  printk(KERN_INFO "first: aio_write_inode file = %p; len = %ld\n", kiocb->ki_filp, len);
  mi = get_mi_by_file(kiocb->ki_filp);
  if (mi) {
    ssize_t (*aio_writep)(struct kiocb *, const struct iovec *, unsigned long, loff_t);
    aio_writep = mi->orig_aio_write;
    //mutex_unlock(mi->inode->i_mutex);
    mutex_unlock(&mi->mi_mutex);
    return aio_writep(kiocb, iovec, len, ofs);    
  }
  return -ENXIO;  
}

static ssize_t sendpage_inode(struct file * file, struct page * page, int i1, size_t s, loff_t * ofs, int i2) {
  struct first_monitored_inode * mi = 0;
  printk(KERN_INFO "first: aio_write_inode file = %p\n", file);
  mi = get_mi_by_file(file);
  if (mi) {
    ssize_t (*sendpagep)(struct file *, struct page *, int, size_t, loff_t *, int);
    sendpagep = mi->orig_sendpage;
    //mutex_unlock(mi->inode->i_mutex);
    mutex_unlock(&mi->mi_mutex);
    return sendpagep(file, page, i1, s, ofs, i2);    
  }
  return -ENXIO;  
}

static ssize_t splice_write_inode(struct pipe_inode_info * pipe, struct file * file,
                                  loff_t * ofs, size_t s, unsigned int ui) {
  struct first_monitored_inode * mi = 0;
  printk(KERN_INFO "first: aio_write_inode file = %p\n", file);
  mi = get_mi_by_file(file);
  if (mi) {
    ssize_t (*splice_writep)(struct pipe_inode_info *, struct file *, loff_t *, size_t, unsigned int);
    splice_writep = mi->orig_splice_write;
    //mutex_unlock(mi->inode->i_mutex);
    mutex_unlock(&mi->mi_mutex);
    return splice_writep(pipe, file, ofs, s, ui);    
  }
  return -ENXIO;  
}

/**
 * @return 0 if not found
 */ 
static struct first_monitored_inode * get_mi_by_file(struct file * file) {
  struct list_head * mi;
  struct first_monitored_inode * _mi;  
  struct list_head * mp;
  struct first_monitoring_process * _mp;  
  int found = 0;
  spin_lock(&file->f_lock);
  printk(KERN_INFO "first: file spin is locked; file = %p;  name = %s\n",
         file, file->f_path.dentry->d_name.name);
  spin_lock(&file->f_path.dentry->d_lock);
  printk(KERN_INFO "first: dentry spin is locked; file = %p; name = %s\n",
         file, file->f_path.dentry->d_name.name);
  mutex_lock(&mp_list_mutex);
  // sequential search; slow but simple!
  // FIXME:  one inode can be monitored multiple processes here 
  //         handler is trigged only the first monitor
  printk(KERN_INFO "first: go over list of montors\n");
  list_for_each(mp, &mp_list) {
    _mp = (struct first_monitoring_process*)mp;
    mutex_lock(&_mp->mp_mutex);
    list_for_each(mi, &_mp->monitored_inodes) {
      _mi = (struct first_monitored_inode*)mi;
      mutex_lock(&_mi->mi_mutex);
      if (_mi->inode == file->f_path.dentry->d_inode) {
        found = 1;        
        break;
      }
      mutex_unlock(&_mi->mi_mutex);
    }    
    mutex_unlock(&_mp->mp_mutex);
    if (found)
        break;    
  }
  mutex_unlock(&mp_list_mutex);
  spin_unlock(&file->f_path.dentry->d_lock);
  printk(KERN_INFO "first dentry spin is unlocked; file = %p\n", file); 
  spin_lock(&file->f_lock);
  printk(KERN_INFO "first: file spin is unlocked; file = %p\n", file); 
  if (found) {
    printk(KERN_INFO "first: inode %p found for file %p\n", _mi->inode, file);
    return _mi;    
  }
  printk(KERN_INFO "first: inode %p is not found for file %p\n", _mi->inode, file);  
  return 0;

}
module_init(first_init);
module_exit(first_cleanup);


MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION("0.1");
