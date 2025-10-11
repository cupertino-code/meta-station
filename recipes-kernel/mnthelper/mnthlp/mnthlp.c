#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/string.h>

static struct kobject *my_kobj;

static char mount_point[100];

static ssize_t mntpoints_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%s\n", mount_point);
}

static ssize_t mntpoints_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    strcpy(mount_point, buf);
    return count;
}

static struct kobj_attribute my_state_attribute =
    __ATTR(mount_point, 0660, mntpoints_show, mntpoints_store);

static int __init mount_helper_init(void)
{
    int result;

    memset(mount_point, 0, sizeof(mount_point));
    my_kobj = kobject_create_and_add("mount_helper", kernel_kobj);
    if (!my_kobj)
        return -ENOMEM;

    result = sysfs_create_file(my_kobj, &my_state_attribute.attr);
    if (result) {
        pr_err("Create sysfs file error: %d\n", result);
        kobject_put(my_kobj);
    }

    pr_info("Module loaded. interface: /sys/kernel/mount_helper/mount_point\n");
    return result;
}

static void __exit mount_helper_exit(void)
{
    sysfs_remove_file(my_kobj, &my_state_attribute.attr);
    kobject_put(my_kobj);
    pr_info("Module unloaded.\n");
}

module_init(mount_helper_init);
module_exit(mount_helper_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Code");
MODULE_DESCRIPTION("USB Mount Helper SysFS interface.");
