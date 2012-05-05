#include <linux/module.h>
//#include <linux/kernel.h>
//#include <linux/config.h>
#include <linux/init.h>
MODULE_LICENSE("GPL-2.0");

static int __init my_init(void) {
  printk(KERN_ALERT "HELLO, WORLD\n");
  return 0;
}
static void __exit my_cleanup(void) {
  printk(KERN_ALERT "GoodBye cruel world\n");
}

module_init(my_init);
module_exit(my_cleanup);
