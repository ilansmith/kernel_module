#include <linux/module.h>

MODULE_AUTHOR ("me");
MODULE_DESCRIPTION("Ceci n'est pas une module");
MODULE_LICENSE ("GPL"); /* this has some legal ramifications */

static int j = 24;
static char *c = "hello";

/* __init: compiler directive that puts it in the init segment. When function
 *         is done with it is disposed of - saves memory) */
static int __init my_entry_point(void)
{
	printk("%s() entered. j is %d, c is %s\n", __FUNCTION__, j, c);
	return 0; /* any return code that is not 0 - module won't be loaded */
}

static void my_exit_point(void)
{
	printk("%s() exited\n", __FUNCTION__);
}

module_init(my_entry_point);
module_exit(my_exit_point);
module_param(j, int, S_IRUSR | S_IWUSR);
module_param(c, charp, S_IRUSR | S_IWUSR);

