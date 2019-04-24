#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>       // for fs function like alloc_chrdev_region / operation file
#include <linux/types.h>
#include <linux/device.h>   // for device_create and class_create
#include <linux/of.h>       // access device tree file
#include <linux/delay.h>
#include <linux/slab.h>     // kmalloc, kcallloc, ....
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/kthread.h>  //kernel threads
#include <linux/sched.h>    //task_struct 
//#include <unistd.h>         // sleep in second

#define PDEBUG(fmt,args...) printk(KERN_DEBUG"%s: "fmt,DRIVER_NAME, ##args)
#define PERR(fmt,args...) printk(KERN_ERR"%s: "fmt,DRIVER_NAME,##args)
#define PINFO(fmt,args...) printk(KERN_INFO"%s: "fmt,DRIVER_NAME, ##args)

#define DRIVER_NAME "pcf8574_7seg"
#define FIRST_MINOR 0
#define BUFF_SIZE 100
#define THREAD_NAME "7seg"

dev_t device_num ;
typedef struct privatedata {
    struct class * dev_class;
    struct device* dev;
    struct i2c_client *client;
    struct task_struct* task;

	struct mutex lock;
} private_data_t;

static int set_7seg(void *param)
{
    private_data_t* data = (private_data_t* )param;
    pid_t pid = current->pid;

    while(1)
    {
        if (kthread_should_stop())
            break;
        PINFO ("thread %s is running\n", THREAD_NAME);
        msleep(3000);
    }

    PINFO ("thread %s stopped, PID: %d\n", THREAD_NAME, pid);
    do_exit(0);
    return 0;
}

/***********************************/
/***** define device attribute *****/
/***********************************/

static ssize_t test_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t len)
{
    private_data_t *data = dev_get_drvdata(dev);
    int ret;
    long value;

    ret = kstrtol(buff, 10, &value);
    if (IS_ERR(ret))
    {
        value = 0;
        PINFO ("value input error: %ld", PTR_ERR(ret));
    }

    i2c_smbus_write_byte(data->client, value);
    PINFO("GPIO change to %02X \n", value);

    return len;
}

static ssize_t test_show(struct device *dev, struct device_attribute *attr, char *buf)
{ 
    int ret;
    private_data_t *data = dev_get_drvdata(dev);

    ret = scnprintf(buf, PAGE_SIZE, "%d\n", i2c_smbus_read_byte(data->client));

    return ret;
}

// create struct device_attribute variable
static DEVICE_ATTR_RW(test);

static struct attribute *device_attrs[] = {
	    &dev_attr_test.attr,
	    NULL
};
ATTRIBUTE_GROUPS(device);

/***************************/
/*****module init + exit****/
/***************************/

static int pcf8574_7seg_probe (struct i2c_client *client,
			 const struct i2c_device_id *id)
{
    int ret;
    private_data_t * private_data;
    struct device * dev;
    struct class * dev_class;
    struct task_struct* task;

    PINFO ("driver pcf8574 init\n");

    // create private data
    private_data = (private_data_t*) kmalloc (sizeof(private_data_t), GFP_KERNEL);

    // create task_struct
    task = (struct task_struct*) kmalloc (sizeof(struct task_struct), GFP_KERNEL);

    // register a device with major and minor number without create device file
    ret = alloc_chrdev_region(&device_num, FIRST_MINOR, 250, DRIVER_NAME); 
    if (ret){
        PERR("Can't register driver, error code: %d \n", ret); 
        goto error;
    }
    PINFO("Succes register driver with major is %d, minor is %d \n", MAJOR(device_num), MINOR(device_num));
    
    // create class 
    dev_class = class_create(THIS_MODULE, DRIVER_NAME);
    if (IS_ERR(dev_class))
    {
        PERR("Class create faili, error code: %ld\n", PTR_ERR(dev_class));

        goto error_class;
    }
    
    // create device and add attribute simultaneously
    dev = device_create_with_groups(dev_class, NULL, device_num, private_data, device_groups, DRIVER_NAME);
    if (IS_ERR(dev))
    {
        PERR("device create fall, error code: %ld\n", PTR_ERR(dev));

        goto error_device;
    }

    // init private data
    private_data->dev_class = dev_class;
    private_data->dev = dev;
    private_data->client = client;
    mutex_init(&private_data->lock);
    task = kthread_run(set_7seg, private_data, THREAD_NAME);
    if (IS_ERR(task))
    {
        PINFO ("Can't create thread to control 7seg led, error: %ld\n", PTR_ERR(task));
    }
    private_data->task = task;
    i2c_set_clientdata(client, private_data);
    

    return 0;

    //error handle
    //device_destroy(device_class, device_num);
error_device:
    class_destroy(dev_class);
error_class:
    unregister_chrdev_region(device_num, FIRST_MINOR); 
error:
    return -1;

}

static int pcf8574_7seg_remove(struct i2c_client *client)
{
    int ret;
    private_data_t *data = i2c_get_clientdata(client);

    PINFO("driver pcf8574 remove from kernel\n");

    ret = kthread_stop(data->task);
    if (ret == -EINTR)
        PINFO ("Process never been wakeup\n");
    PINFO("All child thread stopped\n");
    device_destroy(data->dev_class, device_num);
    class_destroy(data->dev_class);
    unregister_chrdev_region(device_num, FIRST_MINOR); 
    kfree(data);

    return 0;
}

static const struct i2c_device_id pcf8574_7seg_id[]={
    { DRIVER_NAME, 0 },
    {}
};
MODULE_DEVICE_TABLE(i2c, pcf8574_7seg_id);	

static const struct of_device_id pcf8574_of_match[] = {
    { .compatible = "nxp,pcf8574_7seg", },
    {}
};

MODULE_DEVICE_TABLE(of, pcf8574_of_match);

static struct i2c_driver pcf8574_7seg_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .owner = THIS_MODULE,   
        .of_match_table = of_match_ptr (pcf8574_of_match),
    },
    .probe = pcf8574_7seg_probe,
    .remove = pcf8574_7seg_remove,
    .id_table = pcf8574_7seg_id,
};

module_i2c_driver(pcf8574_7seg_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("THOMASTHONG");
MODULE_DESCRIPTION("Seven segment led control via pcf8574 and 74hc595");
