# GStreamer QA system
#
#       test.py
#
# Copyright (c) 2007, Edward Hervey <bilboed@bilboed.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.

import gobject
gobject.threads_init()
import gst
import os
import subprocess
import signal
import time
import dbus
import dbus.gobject_service
from dbustools import unwrap

from log import critical, error, warning, debug, info
import utils

"""
Base Test Classes
"""

# Class tree
#
# Test
# |
# +--- Scenario
# |
# +--- DBusTest
#      |
#      +--- PythonDBusTest
#           |
#           +--- GStreamerTest
#           |
#           +--- CmdLineTest
#

# TODO
#
# Make sure Scenarios can properly be a subclass of Test,
#  if not ==> Create an intermediary class, generalize.

class Test(gobject.GObject):
    """
    Runs a series of commands

    parameters:
    * uuid : unique identifier for the test
        This is assigned by the controlling object
    """
    __test_name__ = "test-base-class"
    __test_description__ = """Base class for tests"""
    __test_arguments__ = { }
    __test_checklist__ = { "test-started":"The test started"}
    __test_timeout__ = 15
    __test_extra_infos__ = {
        "test-setup-duration" : "How long it took to setup the test (in seconds) for asynchronous tests",
        "test-total-duration" : "How long it took to run the entire test (in seconds)"
        }

    # Set to True if your setUp doesn't happen synchronously
    __async_setup__ = False
    # Subclasses need to call ready within that delay (in seconds)
    __async_setup_timeout__ = 10

    # Set to False if you test() method returns immediatly
    __async_test__ = True

    __gsignals__ = {
        "start" : (gobject.SIGNAL_RUN_LAST,
                   gobject.TYPE_NONE,
                   ()),

        "done" : (gobject.SIGNAL_RUN_LAST,
                  gobject.TYPE_NONE,
                  ()),

        "check" : (gobject.SIGNAL_RUN_LAST,
                   gobject.TYPE_NONE,
                   (gobject.TYPE_PYOBJECT, )),

        "extra-info" : (gobject.SIGNAL_RUN_LAST,
                        gobject.TYPE_NONE,
                        (gobject.TYPE_PYOBJECT,
                         gobject.TYPE_PYOBJECT))
        }

    def __init__(self, testrun=None, uuid=None, *args, **kwargs):
        gobject.GObject.__init__(self)
        self._timeout = self.__test_timeout__
        self._running = False
        self.arguments = kwargs
        self._stopping = False

        self._checklist = {}
        # populate checklist with all possible checkitems
        # initialize checklist to False
        self._populateChecklist()
        self._extraInfo = {}

        self._testrun = testrun
        if uuid == None:
            self.uuid = utils.acquire_uuid()
        else:
            self.uuid = uuid
        self.arguments["uuid"] = self.uuid
        self._asynctimeoutid = 0
        self._testtimeoutid = 0
        self._asyncstarttime = 0
        self._teststarttime = 0

    @classmethod
    def get_file(cls):
        """
        Returns the absolute path location of this test.

        This method MUST be copied in all subclasses that are not
        in the same module as its parent !
        """
        import os.path
        return os.path.abspath(cls.__file__)

    def __repr__(self):
        if self.uuid:
            return "< %s uuid:%s >" % (self.__class__.__name__, self.uuid)
        return "< %s id:%p >" % (self.__class__.__name__, id(self))

    def _populateChecklist(self):
        """ fill the instance checklist with default values """
        ckl = self.getFullCheckList()
        for key in ckl.keys():
            self._checklist[key] = False

    def _asyncSetupTimeoutCb(self):
        debug("timeout for %r", self)
        self._asynctimeoutid = 0
        self.stop()
        return False

    def _testTimeoutCb(self):
        debug("timeout for %r", self)
        self._testtimeoutid = 0
        self.stop()
        return False

    def run(self):
        # 1. setUp the test
        self._teststarttime = time.time()
        if not self.setUp():
            error("Something went wrong during setup !")
            self.stop()
            return

        if self.__async_setup__:
            # the subclass will call start() on his own
            # put in a timeout check
            self._asynctimeoutid = gobject.timeout_add(self.__async_setup_timeout__ * 1000,
                                                       self._asyncSetupTimeoutCb)
            return

        # 2. Start it
        self.start()

        if not self.__async_test__:
            # synchronous tests
            self.stop()

    def setUp(self):
        """
        Prepare the test, initialize variables, etc...

        Return True if you setUp didn't encounter any issues, else
        return False.

        If you implement this method, you need to chain up to the
        parent class' setUp() at the BEGINNING of your function without
        forgetting to take into account the return value.

        If your test does its setup asynchronously, set the
        __async_setup__ property of your class to True
        """
        return True

    def tearDown(self):
        """
        Clear test

        If you implement this method, you need to chain up to the
        parent class' setUp() at the END of your method.

        Your teardown MUST happen in a synchronous fashion.
        """
        if self._asynctimeoutid:
            gobject.source_remove(self._asynctimeoutid)
            self._asynctimeoutid = 0
        if self._testtimeoutid:
            gobject.source_remove(self._testtimeoutid)
            self._testtimeoutid = 0

    def stop(self):
        """
        Stop the test
        Can be called by both the test itself AND external elements
        """
        if self._stopping:
            warning("we were already stopping !!!")
            return
        info("STOPPING %r" % self)
        self._stopping = True
        stoptime = time.time()
        self.tearDown()
        self.extraInfo("test-total-duration", stoptime - self._teststarttime)
        self.emit("done")

    def start(self):
        """
        Starts the test.

        Only called by tests that implement asynchronous setUp
        """
        # if we were doing async setup, remove asyncsetup timeout
        if self.__async_setup__:
            gobject.source_remove(self._asynctimeoutid)
            curtime = time.time()
            self.extraInfo("test-setup-duration", curtime - self._teststarttime)
        self.emit("start")
        self.validateStep("test-started")
        # start timeout for test !
        self._testtimeoutid = gobject.timeout_add(self.__test_timeout__ * 1000,
                                                  self._testTimeoutCb)
        self.test()

    def test(self):
        """
        This method will be called at the beginning of the test
        """
        raise NotImplementedError


    ## Methods for tests to return information

    def validateStep(self, checkitem):
        """
        Validate a step in the checklist.
        checkitem is one of the keys of __test_checklist__

        Called by the test itself
        """
        info("step %s for item %r" % (checkitem, self))
        if not checkitem in self._checklist:
            return
        self._checklist[checkitem] = True
        self.emit("check", checkitem)

    def extraInfo(self, key, value):
        """
        Give extra information obtained while running the tests.

        If key was already given, the new value will override the value
        previously given for the same key.

        Called by the test itself
        """
        self._extraInfo[key] = value
        self.emit("extra-info", key, value)

    ## Getters/Setters

    @classmethod
    def getFullCheckList(cls):
        """
        Returns the full test checklist. This is used to know all the
        possible check items for this instance, along with their description.
        """
        d = {}
        for cl in cls.mro():
            if "__test_checklist__" in cl.__dict__:
                d.update(cl.__test_checklist__)
            if cl == Test:
                break
        return d

    @classmethod
    def getFullArgumentList(cls):
        """
        Returns the full list of arguments with descriptions.
        """
        d = {}
        for cl in cls.mro():
            if "__test_arguments__" in cls.__dict__:
                d.update(cl.__test_arguments__)
            if cl == Test:
                break
        return d

    @classmethod
    def getFullExtraInfoList(cls):
        """
        Returns the full list of extra info with descriptions.
        """
        d = {}
        for cl in cls.mro():
            if "__test_extra_infos__" in cls.__dict__:
                d.update(cl.__test_extra_infos__)
            if cl == Test:
                break
        return d

    def getCheckList(self):
        """
        Returns the instance checklist.
        """
        return self._checklist

    def getArguments(self):
        """
        Returns the list of arguments for the given test
        """
        validkeys = self.getFullArgumentList().keys()
        res = {}
        for key in self.arguments.iterkeys():
            if key in validkeys:
                res[key] = self.arguments[key]
        return res

    def getSuccessPercentage(self):
        """
        Returns the success rate of this instance as a float
        """
        ckl = self.getCheckList()
        nbsteps = len(ckl)
        nbsucc = len([x for x in ckl if ckl[x] == True])
        return (100.0 * nbsucc) / nbsteps

    def getExtraInfo(self):
        """
        Returns the extra-information dictionnary
        """
        return self._extraInfo

    def getTimeout(self):
        """
        Returns the currently configured timeout
        """
        return self._timeout

    def setTimeout(self, timeout):
        """
        Set the timeout period for running this test in seconds.
        Returns True if the timeout could be modified, else False.
        """
        # TODO/IDEA : be able to modify the timeout while the test
        # is running
        if self._running:
            # we can't modify the timeout if the test has started
            return False
        self._timeout = timeout
        return True

class DBusTest(Test, dbus.service.Object):
    """
    Class for tests being run in a separate process

    DBus is the ONLY IPC system used for getting results from remote
    tests.
    """
    __test_checklist__ = {
    "dbus-process-spawned":"The DBus child process spawned itself",
    "dbus-process-connected":"The DBus child process connected properly to the private Bus",
    "remote-instance-created":"The remote version of this test was created properly"
    }
    __test_arguments__ = {
    "bus_address":"The private DBUS address used for connections"
    }
    __async_setup__ = True
    ## Needed for dbus
    __metaclass__ = dbus.gobject_service.ExportedGObjectType

    def __init__(self, bus=None, bus_address="", proxy=True,
                 *args, **kwargs):
        """
        bus is the private DBusConnection used for testing.
        bus_address is the address of the private DBusConnection used for testing.

        You need to provide at least bus or bus_address.

        If proxy is set to True, this instance will be the proxy to
        the remote DBus test.
        If proxy is set to False, this instance will be the actual test
        to be run.
        """
        Test.__init__(self, bus_address=bus_address,
                      proxy=proxy, *args, **kwargs)
        self._isProxy = proxy
        if (bus == None) and (bus_address == ""):
            raise Exception("You need to provide at least a bus or bus_address")
        self._bus = bus
        self._bus_address = bus_address

        if self._isProxy:
            if self._testrun:
                self._testrunNewRemoteTestSigId = self._testrun.connect("new-remote-test", self._newRemoteTest)
                self._testrunRemovedRemoteTestSigId = self._testrun.connect("removed-remote-test", self._removedRemoteTest)
            self._process = None
            self._processPollId = 0
            self._remoteInstance = None
        else:
            # connect to bus
            self.objectpath = "/net/gstreamer/Insanity/Test/Test%s" % self.uuid
            dbus.service.Object.__init__(self, conn=self._bus,
                                         object_path=self.objectpath)
    # Test class overrides

    def test(self):
        info("uuid:%s proxy:%r", self.uuid, self._isProxy)
        if self._isProxy:
            self.callRemoteTest()
        else:
            # really do the test
            raise Exception("I shouldn't be called ! I am the remote test !")

    def validateStep(self, checkitem):
        info("uuid:%s proxy:%r checkitem:%s", self.uuid,
             self._isProxy, checkitem)
        if self._isProxy:
            Test.validateStep(self, checkitem)
        else:
            self.remoteValidateStepSignal(checkitem)

    def extraInfo(self, key, value):
        info("uuid:%s proxy:%r", self.uuid, self._isProxy)
        if self._isProxy:
            Test.extraInfo(self, key, value)
        else:
            self.remoteExtraInfoSignal(key, value)


    def setUp(self):
        info("uuid:%s proxy:%r", self.uuid, self._isProxy)
        if Test.setUp(self) == False:
            return False

        if self._isProxy:
            # get the remote launcher
            pargs = self.get_remote_launcher_args()
            env = os.environ.copy()
            env["PRIVATE_DBUS_ADDRESS"] = self._bus_address
            info("Setting PRIVATE_DBUS_ADDRESS : %r" % self._bus_address)
            info("bus:%r" % self._bus)
            # FIXME : monitors !!!

            # spawn the other process
            info("opening %r" % pargs)
            try:
                self._process = subprocess.Popen(pargs, env=env)
            except:
                error("Error starting the subprocess command !")
                return False

            self.validateStep("dbus-process-spawned")
            # add a poller for the proces
            self._processPollId = gobject.timeout_add(500, self._pollSubProcess)
            # Don't forget to set a timeout for waiting for the connection
        else:
            # remote instance setup
            # self.remoteSetUp()
            pass
        return True

    def tearDown(self):
        info("uuid:%s proxy:%r", self.uuid, self._isProxy)
        if self._isProxy:
            # FIXME : tear down the other process gracefully
            #    by first sending it the termination remote signal
            #    and then checking it's killed
            try:
                self.callRemoteStop()
            finally:
                if self._testrun:
                    if self._testrunNewRemoteTestSigId:
                        self._testrun.disconnect(self._testrunNewRemoteTestSigId)
                        self._testrunNewRemoteTestSigId = 0
                    if self._testrunRemovedRemoteTestSigId:
                        self._testrun.disconnect(self._testrunRemovedRemoteTestSigId)
                        self._testrunRemovedRemoteTestSigId = 0
                if self._processPollId:
                    gobject.source_remove(self._processPollId)
                    self._processPollId = 0
                if self._process:
                    # double check it hasn't actually exited
                    while self._process.poll() == None:
                        os.kill(self._process.pid, signal.SIGKILL)
                    self._process = None
        else:
            self.remoteTearDown()
        Test.tearDown(self)

    def get_remote_launcher_args(self):
        """
        Subclasses should return the name and arguments of the remote
        process
        Ex : [ "/path/to/myapp", "--thisoption" ]
        """
        raise NotImplementedError

    ## Subprocess polling
    def _pollSubProcess(self):
        info("polling subprocess")
        if not self._process:
            return False
        res = self._process.poll()
        # None means the process hasn't terminated yet
        if res == None:
            return True
        # Positive value is the return code of the terminated
        #   process
        # Negative values means the process was killed by signal
        info("subprocess returned %r" % res)
        self._process = None
        self._processPollId = 0
        self.stop()
        return False


    ## void handlers for remote DBUS calls
    def voidRemoteCallBackHandler(self):
        pass

    def voidRemoteErrBackHandler(self, exception):
        warning(exception)
        # an error happened, DIVE DIVE DIVE !
        self.stop()


    ## Proxies for remote DBUS calls
    def callRemoteTest(self):
        # call remote instance "remoteTest()"
        if not self._remoteInstance:
            return
        self._remoteInstance.remoteTest(reply_handler=self.voidRemoteCallBackHandler,
                                        error_handler=self.voidRemoteErrBackHandler)

    def callRemoteSetUp(self):
        # call remote instance "remoteSetUp()"
        if not self._remoteInstance:
            return
        self._remoteInstance.remoteSetUp(reply_handler=self.voidRemoteCallBackHandler,
                                         error_handler=self.voidRemoteErrBackHandler)

    def callRemoteStop(self):
        # call remote instance "remoteStop()"
        if not self._remoteInstance:
            return
        self._remoteInstance.remoteStop(reply_handler=self.voidRemoteCallBackHandler,
                                        error_handler=self.voidRemoteErrBackHandler)

    def callRemoteTearDown(self):
        # call remote instance "remoteTearDown()"
        if not self._remoteInstance:
            return
        self._remoteInstance.remoteTearDown(reply_handler=self.voidRemoteCallBackHandler,
                                            error_handler=self.voidRemoteErrBackHandler)

    ## callbacks from remote signals
    def _remoteReadyCb(self):
        info("%s", self.uuid)
        self.start()

    def _remoteStopCb(self):
        info("%s", self.uuid)
        self.stop()

    def _remoteValidateStepCb(self, step):
        info("%s step:%s", self.uuid, step)
        self.validateStep(unwrap(step))

    def _remoteExtraInfoCb(self, key, value):
        info("%s key:%s value:%r", self.uuid, key, value)
        self.extraInfo(unwrap(key), unwrap(value))

    ## Remote DBUS calls
    @dbus.service.method(dbus_interface="net.gstreamer.Insanity.Test",
                         in_signature='', out_signature='')
    def remoteTest(self):
        raise NotImplementedError

    @dbus.service.method(dbus_interface="net.gstreamer.Insanity.Test",
                         in_signature='', out_signature='')
    def remoteSetUp(self):
        info("%s", self.uuid)
        # if not overriden, we just emit the "ready" signal
        self.remoteReadySignal()

    @dbus.service.method(dbus_interface="net.gstreamer.Insanity.Test",
                         in_signature='', out_signature='')
    def remoteStop(self):
        info("%s", self.uuid)
        self.stop()

    @dbus.service.method(dbus_interface="net.gstreamer.Insanity.Test",
                         in_signature='', out_signature='')
    def remoteTearDown(self):
        info("%s", self.uuid)

    ## Remote DBUS signals
    @dbus.service.signal(dbus_interface="net.gstreamer.Insanity.Test",
                         signature='')
    def remoteReadySignal(self):
        info("%s", self.uuid)

    @dbus.service.signal(dbus_interface="net.gstreamer.Insanity.Test",
                         signature='')
    def remoteStopSignal(self):
        info("%s", self.uuid)

    @dbus.service.signal(dbus_interface="net.gstreamer.Insanity.Test",
                         signature='')
    def remoteStartSignal(self):
        info("%s", self.uuid)

    @dbus.service.signal(dbus_interface="net.gstreamer.Insanity.Test",
                         signature='s')
    def remoteValidateStepSignal(self, step):
        info("%s %s", self.uuid, step)

    @dbus.service.signal(dbus_interface="net.gstreamer.Insanity.Test",
                         signature='sv')
    def remoteExtraInfoSignal(self, name, data):
        info("%s %s : %r", self.uuid, name, data)

    ## DBUS Signals for proxies

    def _newRemoteTest(self, testrun, uuid):
        if not uuid == self.uuid:
            return

        info("%s our remote counterpart has started", self.uuid)
        self.validateStep("dbus-process-connected")
        # we need to give the remote process the following information:
        # * filename where the Test class is located (self.get_file())
        # * class name (self.__class__.__name__)
        # * the arguments (self.arguments) + proxy=True
        rname = "net.gstreamer.Insanity.Test.Test%s" % self.uuid
        rpath = "/net/gstreamer/Insanity/Test/RemotePythonRunner%s" % self.uuid
        # get the proxy object to our counterpart
        remoteRunnerObject = self._bus.get_object(rname, rpath)
        debug("Got remote runner object %r" % remoteRunnerObject)
        # call createTestInstance()
        remoteRunner = dbus.Interface(remoteRunnerObject,
                                      "net.gstreamer.Insanity.RemotePythonRunner")
        debug("Got remote iface %r" % remoteRunner)
        remoteRunner.createTestInstance(self.get_file(),
                                        self.__module__,
                                        self.__class__.__name__,
                                        self.arguments,
                                        reply_handler=self._createTestInstanceCallBack,
                                        error_handler=self.voidRemoteErrBackHandler)

    def _createTestInstanceCallBack(self, retval):
        debug("%s retval:%r", self.uuid, retval)
        if retval:
            self.validateStep("remote-instance-created")
            rname = "net.gstreamer.Insanity.Test.Test%s" % self.uuid
            rpath = "/net/gstreamer/Insanity/Test/Test%s" % self.uuid
            # remote instance was successfully created, let's get it
            remoteObj = self._bus.get_object(rname, rpath)
            self._remoteInstance = dbus.Interface(remoteObj,
                                                  "net.gstreamer.Insanity.Test")
            self._remoteInstance.connect_to_signal("remoteReadySignal",
                                                   self._remoteReadyCb)
            self._remoteInstance.connect_to_signal("remoteStopSignal",
                                                   self._remoteStopCb)
            self._remoteInstance.connect_to_signal("remoteValidateStepSignal",
                                                   self._remoteValidateStepCb)
            self._remoteInstance.connect_to_signal("remoteExtraInfoSignal",
                                                   self._remoteExtraInfoCb)
            self.callRemoteSetUp()
        else:
            self.stop()

    def _removedRemoteTest(self, testrun, uuid):
        if not uuid == self.uuid:
            return

        info("%s our remote counterpart has left", self.uuid)
        # abort if the test hasn't actually finished
        self._remoteInstance = None
        self.stop()

class PythonDBusTest(DBusTest):
    """
    Convenience class for python-based tests being run in a separate process
    """

    def get_remote_launcher_args(self):
        import os
        # FIXME : add proper arguments
        # locate the python dbus runner
        # HACK : take top-level-dir/bin/pythondbusrunner.py
        rootdir = os.path.split(os.path.dirname(os.path.abspath(__file__)))[0]
        return [os.path.join(rootdir, "bin", "pythondbusrunner.py"), self.uuid]

class GStreamerTest(PythonDBusTest):
    """
    Tests that specifically run a GStreamer pipeline
    """
    __test_checklist__ = {
        "valid-pipeline" : "The test pipeline was properly created",
        "reached-initial-state" : "The pipeline reached the initial GstElementState",
        "no-errors-seen" : "No errors were emitted from the pipeline"
        }

    __test_extra_infos__ = {
        "errors" : "List of errors emitted by the pipeline",
        "tags" : "List of tags emitted by the pipeline"
        }
    # Initial pipeline state, subclasses can override this
    __pipeline_initial_state__ = gst.STATE_PLAYING

    def remoteSetUp(self):
        debug("%s", self.uuid)
        # local variables
        self._errors = []
        self._tags = {}

        # create the pipeline
        self.pipeline = self.createPipeline()
        if not self.pipeline == None:
            self.validateStep("valid-pipeline")
        else:
            self.stop()
            return

        # connect to bus
        self.bus = self.pipeline.get_bus()
        self.bus.add_signal_watch()
        self.bus.connect("message", self._busMessageHandlerCb)
        self.remoteReadySignal()

    def remoteTearDown(self):
        # unref pipeline and so forth
        if self.pipeline:
            self.pipeline.set_state(gst.STATE_NULL)
        if self._errors == []:
            self.validateStep("no-errors-seen")
        else:
            self.remoteExtraInfoSignal("errors", self._errors)
        if not self._tags == {}:
            self.remoteExtraInfoSignal("tags", dbus.Dictionary(self._tags, signature="sv"))

    def remoteTest(self):
        # kickstart pipeline to initial state
        debug("Setting pipeline to initial state %r", self.__pipeline_initial_state__)
        res = self.pipeline.set_state(self.__pipeline_initial_state__)

    def _busMessageHandlerCb(self, bus, message):
        debug("%s from %r message:%r", self.uuid, message.src, message)
        # let's pass it on to subclass to see if they want us to ignore that message
        if self.handleBusMessage(message) == False:
            debug("ignoring message")
            return
        # handle common types
        if message.type == gst.MESSAGE_ERROR:
            error, dbg = message.parse_error()
            self._errors.append((error.code, error.domain, error.message, dbg))
            self.stop()
        elif message.type == gst.MESSAGE_TAG:
            self._gotTags(message.parse_tag())
        elif message.src == self.pipeline:
            if message.type == gst.MESSAGE_EOS:
                self.stop()
            elif message.type == gst.MESSAGE_STATE_CHANGED:
                prev, cur, pending = message.parse_state_changed()
                if cur == self.__pipeline_initial_state__ and pending == gst.STATE_VOID_PENDING:
                    if self.pipelineReachedInitialState():
                        debug("Stopping test because we reached initial state")
                        self.validateStep("reached-initial-state")
                        self.stop()

    def _gotTags(self, tags):
        for key in tags.keys():
            value = tags[key]
            if isinstance(value, gobject.GBoxed):
                value = repr(value)
            elif isinstance(value, gst.MiniObject):
                value = repr(value)
            self._tags[key] = value

    ## Methods that can be overridden by subclasses

    def pipelineReachedInitialState(self):
        """
        Override this method to implement some behaviour once your pipeline
        has reached the initial state.

        Return True if you want the test to stop (default behaviour).
        Return False if you want the test to carry on (most likely because you
        wish to do other actions/testing).
        """
        return True

    def handleBusMessage(self, message):
        """
        Override this method if you want to be able to handle messages from the
        bus.

        Return False if you don't want the base class to handle it (because you
        have been handling the Error messages or EOS messages and you don't
        want the base class to do the default handling.
        Else return True.
        """
        return True

    def getPipelineString(self):
        """
        Return the pipeline string for the given test.
        This method should be implemented in tests that don't create the
        pipeline manually, but instead can just return a parse-launch syntax
        string representation of the pipeline.
        """
        raise NotImplementedError

    def createPipeline(self):
        """
        Construct and return the pipeline for the given test

        Return a gst.Pipeline if creation was successful.
        Return None if an error occured.
        """
        # default implementation : ask for parse-launch syntax
        pipestring = self.getPipelineString()
        debug("%s Got pipeline string %s", self.uuid, pipestring)
        try:
            p = gst.parse_launch(pipestring)
        except:
            warning("error while creating pipeline")
            p = None
        return p

class CmdLineTest(PythonDBusTest):
    """
    Tests that run a command line application/script.
    """
    # TODO : fill with command line generic stuff
    pass
