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

#define PDEBUG(fmt,args...) printk(KERN_DEBUG"%s: "fmt,DRIVER_NAME, ##args)
#define PERR(fmt,args...) printk(KERN_ERR"%s: "fmt,DRIVER_NAME,##args)
#define PINFO(fmt,args...) printk(KERN_INFO"%s: "fmt,DRIVER_NAME, ##args)

#define DRIVER_NAME "pcf8574_7seg"
#define FIRST_MINOR 0
#define BUFF_SIZE 100
#define THREAD_NAME "7seg"

#define COMMON_ANODE
//#define COMMON_CATHODE
#define DIGIT 4
#define NUM_DIGIT 8
#define SCLK 7
#define RCLK 6
#define DIO 5
#define FREQ 60

const int segment[] =
{// 0    1    2    3    4    5    6    7    8    9    A    b    C    d    E    F    -
  0xC0,0xF9,0xA4,0xB0,0x99,0x92,0x82,0xF8,0x80,0x90,0x8C,0xBF,0xC6,0xA1,0x86,0xFF,0xbf
};
const uint8_t default_value = (1<<SCLK) | (1<<RCLK) | (1<<DIO);

dev_t device_num ;
typedef struct privatedata {
    struct class * dev_class;
    struct device* dev;
    struct i2c_client *client;
    struct task_struct* task;
    uint32_t num;
    uint8_t buff[100];
    uint8_t buff_index;
    uint8_t current_data;

	struct mutex lock;
} private_data_t;

void sclk_up (private_data_t *data)
{
    data->buff[data->buff_index++] = data->current_data & ~(1 << SCLK);
    data->buff[data->buff_index++] = data->current_data | (1 << SCLK);

    data->current_data = data->buff[data->buff_index - 1];
}

void rclk_up (private_data_t *data)
{
    data->buff[data->buff_index++] = data->current_data & ~(1 << RCLK);
    data->buff[data->buff_index++] = data->current_data | (1 << RCLK);

    data->current_data = data->buff[data->buff_index - 1];
}

void set_data(private_data_t *data, bool bit)
{
    #ifdef COMMON_ANODE
    if (bit)
    {
        data->buff[data->buff_index++] = data->current_data | (1 << DIO); 
        data->current_data = data->buff[data->buff_index - 1];
    }

    else 
    {
        data->buff[data->buff_index++] = data->current_data & ~(1 << DIO);
        data->current_data = data->buff[data->buff_index - 1];
    }
        
    #elif COMMON_CATHODE
    if (bit)
        data->buff[data->buff_index++] = data->current_data & ~(1 << DIO); 
    else 
        data->buff[data->buff_index++] = data->current_data | (1 << DIO);
    //#else 
    #endif

    
}

void send_data(private_data_t *data)
{
    i2c_master_send(data->client, data->buff, data->buff_index);
    data->buff_index = 0;
}

static int set_7seg(void *param)
{
    private_data_t* data = (private_data_t* )param;
    pid_t pid = current->pid;
    int delay = 1000 / FREQ, i, j, buff_index = 0, num_digit = 0;
    bool bit ;

    #ifdef NUM_DIGIT
    num_digit = NUM_DIGIT;
    #else
    num_digit = DIGIT;
    #endif

    PINFO ("7seg led on\n");
    while(1)
    {
        if (kthread_should_stop())
            break;

        int num = data->num, index;
        
        for (i = DIGIT; i >= 0 ; --i)
        {
            index = num % 10;
            num /= 10;
            
            // set led value
            for (j = 0; j < 8; ++j)
            {
                set_data(data,(segment[index] << j) & 0x80);
                sclk_up(data);
            }

            // set digit
            for (j = 0; j < num_digit; ++j)
            {
                if (j != i)
                    set_data(data, 0);
                else 
                    set_data(data, 1);
                sclk_up(data);
            }
            rclk_up(data);

            if (kthread_should_stop())
                break;
                send_data(data);
        }
    }

    PINFO ("thread %s stopped, PID: %d\n", THREAD_NAME, pid);
    do_exit(0);
    return 0;
}

/***********************************/
/***** define device attribute *****/
/***********************************/

static ssize_t number_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t len)
{
    private_data_t *data = dev_get_drvdata(dev);
    int ret;
    long value = 0, temp = len, i;

    // check if input is valid
    ret = kstrtol(buff, 10, &value);
    if (ret)
    {
        value = 0;
        PINFO ("Can only input number, value input error: %d", ret);
    }
    else{
        mutex_lock(&data->lock);
        data->num = value;
        mutex_unlock(&data->lock);
    }

    return len;
}

static ssize_t number_show(struct device *dev, struct device_attribute *attr, char *buf)
{ 
    int ret;
    private_data_t *data = dev_get_drvdata(dev);

    ret = scnprintf(buf, PAGE_SIZE, "%ld\n", data->num);

    return ret;
}

// create struct device_attribute variable
static DEVICE_ATTR_RW(number);

static struct attribute *device_attrs[] = {
	    &dev_attr_number.attr,
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
    private_data = (private_data_t*) kcalloc (1, sizeof(private_data_t), GFP_KERNEL);

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
    private_data->buff_index = 0;
    private_data->current_data = default_value;
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
