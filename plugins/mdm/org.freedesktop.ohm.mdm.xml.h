const char *mdm_plugin_introspect_string = "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n<node>\n    <interface name=\"org.freedesktop.ohm.mdm\">\n    <!-- since InterfaceVersion 1 -->\n        <method name=\"InterfaceVersion\">\n            <arg name=\"version\" type=\"u\" direction=\"out\"/>\n        </method>\n        <method name=\"GetAll\">\n            <arg name=\"entries\" type=\"a(ss)\" direction=\"out\"/>\n        </method>\n        <method name=\"Get\">\n            <arg name=\"name\" type=\"s\" direction=\"in\"/>\n            <arg name=\"value\" type=\"s\" direction=\"out\"/>\n        </method>\n        <method name=\"Set\">\n            <arg name=\"name\" type=\"s\" direction=\"in\"/>\n            <arg name=\"value\" type=\"s\" direction=\"in\"/>\n        </method>\n        <signal name=\"ValueChanged\">\n            <arg name=\"name\" type=\"s\"/>\n            <arg name=\"value\" type=\"s\"/>\n        </signal>\n    </interface>\n</node>\n";
