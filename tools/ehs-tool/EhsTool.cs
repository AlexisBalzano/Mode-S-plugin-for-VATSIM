// EHS Link test tool.
//   EhsTool.exe sim <CALLSIGN> [seconds] [lat] [lon]
//       Acts as a pilot: publishes changing selections on the live broker with the same client
//       code as the vPilot plugin (Last Will, retained, on-change + heartbeat), then clears.
//   EhsTool.exe load <path\to\EhsLink.vPilot.dll> [seconds]
//       Loads the vPilot plugin exactly like vPilot does, with a fake broker that "connects"
//       as TEST123, and prints the plugin's vPilot messages.

using System;
using System.Text;
using System.Threading;
using EhsLink;
using RossCarlson.Vatsim.Vpilot.Plugins;
using RossCarlson.Vatsim.Vpilot.Plugins.Events;

class FakeBroker : IBroker
{
    public event EventHandler SessionEnded;
    public event EventHandler<NetworkConnectedEventArgs> NetworkConnected;
    public event EventHandler NetworkDisconnected;
#pragma warning disable 67
    public event EventHandler<PrivateMessageReceivedEventArgs> PrivateMessageReceived;
    public event EventHandler<RadioMessageReceivedEventArgs> RadioMessageReceived;
    public event EventHandler<BroadcastMessageReceivedEventArgs> BroadcastMessageReceived;
    public event EventHandler<MetarReceivedEventArgs> MetarReceived;
    public event EventHandler<AtisReceivedEventArgs> AtisReceived;
    public event EventHandler<ControllerAddedEventArgs> ControllerAdded;
    public event EventHandler<ControllerDeletedEventArgs> ControllerDeleted;
    public event EventHandler<ControllerFrequencyChangedEventArgs> ControllerFrequencyChanged;
    public event EventHandler<ControllerLocationChangedEventArgs> ControllerLocationChanged;
    public event EventHandler<SelcalAlertReceivedEventArgs> SelcalAlertReceived;
    public event EventHandler<AircraftAddedEventArgs> AircraftAdded;
    public event EventHandler<AircraftUpdatedEventArgs> AircraftUpdated;
    public event EventHandler<AircraftDeletedEventArgs> AircraftDeleted;
#pragma warning restore 67

    public void RequestConnect(string c, string t, string s) { }
    public void RequestConnectAsObserver(string c) { }
    public void RequestDisconnect() { }
    public void RequestMetar(string s) { }
    public void RequestAtis(string c) { }
    public void SendPrivateMessage(string to, string m) { }
    public void SendRadioMessage(string m) { }
    public void PostDebugMessage(string m) { Console.WriteLine("[vPilot] " + m); }
    public void SetModeC(bool m) { }
    public void SquawkIdent() { }
    public void SetPtt(bool p) { }

    public void Connect(string cs) { NetworkConnected?.Invoke(this, new NetworkConnectedEventArgs("1234567", cs, "B77W", "", false)); }
    public void Disconnect() { NetworkDisconnected?.Invoke(this, EventArgs.Empty); }
    public void End() { SessionEnded?.Invoke(this, EventArgs.Empty); }
}

static class Program
{
    static int Main(string[] args)
    {
        if (args.Length >= 2 && args[0] == "sim") return Sim(args[1].ToUpperInvariant(), args.Length > 2 ? int.Parse(args[2]) : 30,
            args.Length > 3 ? double.Parse(args[3], System.Globalization.CultureInfo.InvariantCulture) : 51.4706,
            args.Length > 4 ? double.Parse(args[4], System.Globalization.CultureInfo.InvariantCulture) : -0.4619);
        if (args.Length >= 1 && args[0] == "fsuipc")   // cross-check PMDG 777 MCP values through FSUIPC
        {
            var r = FsuipcPeek.Read(0x6554, 4, 0x655A, 2, 0x655C, 2, 0x655E, 2, 0x3D00, 64);
            if (r == null) { Console.WriteLine("FSUIPC not reachable"); return 1; }
            int z = Array.IndexOf(r[4], (byte)0);
            Console.WriteLine("title: " + System.Text.Encoding.Default.GetString(r[4], 0, z < 0 ? 64 : z));
            Console.WriteLine("PMDG 777 MCP via FSUIPC: IAS/Mach " + BitConverter.ToSingle(r[0], 0) + "  HDG " + BitConverter.ToUInt16(r[1], 0) +
                              "  ALT " + BitConverter.ToUInt16(r[2], 0) + "  V/S " + BitConverter.ToInt16(r[3], 0) + "   (all zero = PMDG not broadcasting)");
            return 0;
        }
        if (args.Length >= 1 && args[0] == "probe")   // read the MSFS module like the plugin does
        {
            int seconds = args.Length > 1 ? int.Parse(args[1]) : 5;
            using (var sim = new ModuleReader())
            {
                sim.Start();
                Thread.Sleep(2500);   // connect + first data
                for (int i = 0; i < seconds; i++)
                {
                    Selections sel;
                    var st = sim.Read(out sel);
                    Console.WriteLine("status=" + st + " aircraft='" + sim.LastTitle + "'" + (sel != null ? " json=" + sel.ToJson("PROBE", 0) : ""));
                    Thread.Sleep(1000);
                }
            }
            return 0;
        }
        if (args.Length >= 2 && args[0] == "load") return Load(args[1], args.Length > 2 ? int.Parse(args[2]) : 8);
        Console.WriteLine("Usage: EhsTool sim <CALLSIGN> [seconds] [lat] [lon] | EhsTool load <plugin.dll> [seconds]");
        return 1;
    }

    static int Sim(string cs, int seconds, double lat, double lon)
    {
        var topic = EhsLinkPlugin.TopicRoot + cs;
        var mqtt = new MqttClient();
        mqtt.Connect("broker.hivemq.com", 1883, "ehs-sim-" + Guid.NewGuid().ToString("N").Substring(0, 8), 30, topic, new byte[0], true);
        Console.WriteLine("Connected; publishing " + topic);
        var sel = new Selections { Source = "SIM", Lat = lat, Lon = lon, Alt = 24000, Hdg = 270, Mach = 0.78 };
        var start = DateTime.UtcNow;
        string lastKey = null; var lastSent = DateTime.MinValue;
        while ((DateTime.UtcNow - start).TotalSeconds < seconds)
        {
            var t = (DateTime.UtcNow - start).TotalSeconds;
            // scripted changes: wrong level at 5 s, heading at 10 s, IAS + V/S at 15 s
            if (t > 5) sel.Alt = 23000;
            if (t > 10) sel.Hdg = 250;
            if (t > 15) { sel.Mach = null; sel.Ias = 280; sel.Vs = -1500; }
            var key = sel.Key(); var since = (DateTime.UtcNow - lastSent).TotalMilliseconds;
            if ((key != lastKey && since >= 1000) || since >= 10000)
            {
                var json = sel.ToJson(cs, DateTimeOffset.UtcNow.ToUnixTimeSeconds());
                mqtt.Publish(topic, Encoding.UTF8.GetBytes(json), true);
                Console.WriteLine("PUB " + json);
                lastKey = key; lastSent = DateTime.UtcNow;
            }
            Thread.Sleep(250);
        }
        mqtt.Publish(topic, new byte[0], true);
        Console.WriteLine("Cleared " + topic);
        mqtt.Disconnect();
        return 0;
    }

    static int Load(string path, int seconds)
    {
        var broker = new FakeBroker();
        IPlugin plugin = null;
        foreach (var t in System.Reflection.Assembly.LoadFrom(path).GetTypes())
            if (typeof(IPlugin).IsAssignableFrom(t) && !t.IsAbstract) plugin = (IPlugin)Activator.CreateInstance(t);
        if (plugin == null) { Console.WriteLine("No IPlugin found"); return 1; }
        Console.WriteLine("Loaded plugin: " + plugin.Name);
        plugin.Initialize(broker);
        broker.Connect("TEST123");
        Thread.Sleep(seconds * 1000);
        broker.Disconnect();
        broker.End();
        Thread.Sleep(500);
        return 0;
    }
}
