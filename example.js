var udev = require("./udev");

//console.log(udev.list()); // this is a long list :)

var monitor = udev.monitor();
monitor.on('add', function (device) {
    console.log('added ' + device);
    console.log(udev.getMountPoint(device));
});
monitor.on('remove', function (device) {
    console.log('removed ' + device);
});
monitor.on('change', function (device) {
    console.log('changed ' + device);
});
