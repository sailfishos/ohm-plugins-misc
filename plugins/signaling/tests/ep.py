#!/usr/bin/python

import gobject

import dbus
import dbus.service
import dbus.mainloop.glib

import sys

# for the event scheduler
import threading
import time
import sched


DBUS_NAME_OHM             = "org.freedesktop.ohm"
DBUS_INTERFACE_POLICY     = "com.nokia.policy"
DBUS_INTERFACE_FDO        = "org.freedesktop.DBus"
DBUS_PATH_POLICY          = "/com/nokia/policy"

METHOD_POLICY_REGISTER    = "register"
METHOD_POLICY_UNREGISTER  = "unregister"
SIGNAL_POLICY_ACK         = "status"
SIGNAL_POLICY_ACTIONS     = "actions"

mainloop = None

class EventScheduler(threading.Thread):

    def __init__(self, **kwds):
        threading.Thread.__init__(self, **kwds)
        self.setDaemon(True)
        
        # scheduler for timed signals
        self.scheduler = sched.scheduler(time.time, time.sleep)
        self.condition = threading.Condition()

    def run(self):
        while True:
            # note that this is should be actually thread-safe
            if self.scheduler.empty():
                # there are no events to schedule, let's wait
                self.condition.acquire()
                self.condition.wait()
                # Note: after wait, this acquires the lock again. That's
                # why we need to release, so that we can add events
                self.condition.release()
            
            # print "started running events"
            self.scheduler.run()
            # print "stopped running events"

    def add_event(self, delay, function, *args):
        self.scheduler.enter(delay, 1, function, args)
        # tell the runner to stop waiting
        self.condition.acquire()
        self.condition.notifyAll()
        self.condition.release()

class ExternalEnforcementPoint(dbus.service.Object):

    def __init__(self):
        self.event_scheduler = EventScheduler();

        self.bus = dbus.SystemBus()

        self.ohm_proxy = self.bus.get_object(DBUS_NAME_OHM, DBUS_PATH_POLICY)
        self.bus.add_signal_receiver(self.ep_signal_received, SIGNAL_POLICY_ACTIONS, None, None, None)
        dbus.service.Object.__init__(self, self.bus, DBUS_PATH_POLICY)
        # register to ohmd
        self.ohm_proxy.register(dbus_interface=DBUS_INTERFACE_POLICY)
    
    @dbus.service.signal(DBUS_INTERFACE_POLICY, signature="uu")
    def status(self, txid, status):
        if status != 0:
            print "emitting ACK signal"
        else:
            print "emitting NACK signal"

    def ep_signal_received(self, *args):
        txid = args[0]
        print "D-Bus signal received with txid: '" + str(txid) + "'"
        decisions = args[1]
        for command in decisions:
            print "command: '" + command + "'"
            facts = decisions[command]
            for fact in facts:
                print "  Fact:"
                for struct in fact:
                    print "    tag: '" + struct[0] + "', value: '" + struct[1] + "'"

        if len(args) == 2:
            if args[0] != 0:
                self.event_scheduler.add_event(1, self.status(args[0], dbus.UInt32(1)))

def main(argv=None):
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    mainloop = gobject.MainLoop()
    gobject.threads_init() # argh! let's not forget this the next time
    eep_daemon = ExternalEnforcementPoint()
    mainloop.run()

if __name__ == "__main__":
    sys.exit(main())

