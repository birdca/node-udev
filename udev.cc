#include <v8.h>
#include <node.h>
#include <nan.h>

#include <libudev.h>
#include <mntent.h>
#include <unistd.h>

using namespace v8;

static struct udev *udev;

static void PushProperties(Local<Object> obj, struct udev_device* dev)
{
    struct udev_list_entry* sysattrs;
    struct udev_list_entry* entry;

    sysattrs = udev_device_get_properties_list_entry(dev);
    udev_list_entry_foreach(entry, sysattrs)
    {
        const char *name, *value;

        name = udev_list_entry_get_name(entry);
        value = udev_list_entry_get_value(entry);
        if (value != NULL)
        {
            obj->Set(NanNew(name), NanNew(value));
        }
        else
        {
            obj->Set(NanNew(name), NanNull());
        }
    }
}

class Monitor : public node::ObjectWrap
{
    struct poll_struct
    {
        Persistent<Object> monitor;
    };

    uv_poll_t* poll_handle;
    udev_monitor* mon;
    int fd;

    static void on_handle_event(uv_poll_t* handle, int status, int events)
    {
        NanScope();

        poll_struct* data = (poll_struct*)handle->data;
        Local<Object> monitor = NanNew(data->monitor);
        Monitor* wrapper = ObjectWrap::Unwrap<Monitor>(monitor);
        udev_device* dev = udev_monitor_receive_device(wrapper->mon);

        Local<Object> obj = NanNew<Object>();
        obj->Set(NanNew("syspath"), NanNew(udev_device_get_syspath(dev)));
        PushProperties(obj, dev);

        TryCatch tc;
        Local<Function> emit = monitor->Get(NanNew("emit")).As<Function>();
        Local<Value> emitArgs[3];
        emitArgs[0] = NanNew(udev_device_get_action(dev));
        emitArgs[1] = NanNew(udev_device_get_devnode(dev));
        emitArgs[2] = obj;
        emit->Call(monitor, 3, emitArgs);

        udev_device_unref(dev);
        if (tc.HasCaught()) node::FatalException(tc);
    };

    static NAN_METHOD(New)
    {
        NanScope();

        uv_poll_t* handle;
        Monitor* obj = new Monitor();
        obj->Wrap(args.This());

        /* Set up a monitor to monitor usb devices */
        obj->mon = udev_monitor_new_from_netlink(udev, "udev");
        udev_monitor_filter_add_match_subsystem_devtype(obj->mon, "block", "partition");
        udev_monitor_enable_receiving(obj->mon);

        /* Get the file descriptor (fd) for the monitor. */
        obj->fd = udev_monitor_get_fd(obj->mon);

        obj->poll_handle = handle = new uv_poll_t;

        /* Make the call to receive the device. */
        udev_monitor_enable_receiving(obj->mon);

        poll_struct* data = new poll_struct;
        NanAssignPersistent(data->monitor, args.This());
        handle->data = data;
        uv_poll_init(uv_default_loop(), obj->poll_handle, obj->fd);
        uv_poll_start(obj->poll_handle, UV_READABLE, on_handle_event);
        NanReturnThis();
    }

    static void on_handle_close(uv_handle_t *handle)
    {
        poll_struct* data = (poll_struct*)handle->data;
        NanDisposePersistent(data->monitor);

        delete data;
        delete handle;
    }

    static NAN_METHOD(Close)
    {
        NanScope();

        Monitor* obj = ObjectWrap::Unwrap<Monitor>(args.This());
        uv_poll_stop(obj->poll_handle);
        uv_close((uv_handle_t*)obj->poll_handle, on_handle_close);
        udev_monitor_unref(obj->mon);
        NanReturnUndefined();
    }

    public:
    static void Init(Handle<Object> target)
    {
        // I do not remember why the functiontemplate was tugged into a persistent.
        static Persistent<FunctionTemplate> constructor;

        Local<FunctionTemplate> tpl = NanNew<FunctionTemplate>(New);
        tpl->SetClassName(NanNew("Monitor"));
        tpl->InstanceTemplate()->SetInternalFieldCount(1);

        NODE_SET_PROTOTYPE_METHOD(tpl, "close", Close);
        NanAssignPersistent(constructor, tpl);
        target->Set(NanNew("Monitor"), NanNew(tpl)->GetFunction());
    }
};

NAN_METHOD(List)
{
    NanScope();

    Local<Array> list = NanNew<Array>();

    struct udev_enumerate  *enumerate;
    struct udev_list_entry *devices;
    struct udev_list_entry *entry;
    struct udev_device     *dev;

    enumerate = udev_enumerate_new(udev);

    // add match etc. stuff.
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);

    int i = 0;
    udev_list_entry_foreach(entry, devices)
    {
        const char *path;
        path = udev_list_entry_get_name(entry);
        dev  = udev_device_new_from_syspath(udev, path);
        Local<Object> obj = NanNew<Object>();
        PushProperties(obj, dev);
        obj->Set(NanNew("syspath"), NanNew(path));
        list->Set(i++, obj);
        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);
    NanReturnValue(list);
}

NAN_METHOD(GetMountPoint)
{
    NanScope();

    v8::String::Utf8Value pathObj(args[0]->ToString());

    struct mntent   *mnt;
    FILE            *fp   = NULL;
    const char      *path = *pathObj;

    // TODO: find a better way to replace waiting for a second
    sleep(1);
    if ((fp = setmntent("/proc/mounts", "r")) == NULL)
    {
        NanThrowError("Can't open mounted filesystems\n");
        return;
    }

    while ((mnt = getmntent(fp)))
    {
        if (!strcmp(mnt->mnt_fsname, path))
            NanReturnValue(NanNew(mnt->mnt_dir));
    }

    /* close file for describing the mounted filesystems */
    endmntent(fp);
}

static void Init(Handle<Object> target)
{
    /* Create the udev object */
    udev = udev_new();
    if (!udev)
    {
        NanThrowError("Can't create udev\n");
    }

    target->Set(NanNew("list"), NanNew<FunctionTemplate>(List)->GetFunction());
    target->Set(NanNew("getMountPoint"), NanNew<FunctionTemplate>(GetMountPoint)->GetFunction());

    Monitor::Init(target);
}

NODE_MODULE(udev, Init)
