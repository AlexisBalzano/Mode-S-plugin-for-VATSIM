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
        if (args.Length >= 1 && args[0] == "snapshot")   // EhsTool snapshot [value ...]  (no FSUIPC needed)
            return Snapshot(args);
        if (args.Length >= 2 && args[0] == "lvars")   // EhsTool lvars NAME1 NAME2 ...  (live values via FSUIPC)
        {
            var names = new string[args.Length - 1];
            Array.Copy(args, 1, names, 0, names.Length);
            var v = FsuipcPeek.ReadLvars(names);
            if (v == null) { Console.WriteLine("FSUIPC not reachable"); return 1; }
            for (int i = 0; i < names.Length; i++) Console.WriteLine("  " + names[i].PadRight(32) + (double.IsNaN(v[i]) ? "(not found)" : v[i].ToString("0.###")));
            return 0;
        }
        if (args.Length >= 1 && args[0] == "apvars")   // standard AP variables at the indexes FSUIPC exposes
        {
            var r = FsuipcPeek.Read(0x07CC, 2, 0x07CE, 2, 0x07D4, 4, 0x0818, 4, 0x07E2, 2, 0x07F2, 2);
            if (r == null) { Console.WriteLine("FSUIPC not reachable"); return 1; }
            Console.WriteLine("HDG LOCK DIR      (07CC) " + Math.Round(BitConverter.ToUInt16(r[0], 0) * 360.0 / 65536.0));
            Console.WriteLine("HDG LOCK DIR:3    (07CE) " + Math.Round(BitConverter.ToUInt16(r[1], 0) * 360.0 / 65536.0));
            Console.WriteLine("ALT LOCK VAR      (07D4) " + Math.Round(BitConverter.ToInt32(r[2], 0) / 65536.0 * 3.28084) + " ft");
            Console.WriteLine("ALT LOCK VAR:3    (0818) " + Math.Round(BitConverter.ToInt32(r[3], 0) / 65536.0) + " (/65536)");
            Console.WriteLine("AIRSPEED HOLD VAR (07E2) " + BitConverter.ToUInt16(r[4], 0) + " kt");
            Console.WriteLine("VERTICAL HOLD VAR (07F2) " + BitConverter.ToInt16(r[5], 0) + " fpm");
            return 0;
        }
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

    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential, Pack = 1)]
    struct ControlRequest { public int counter; }
    enum SnapCd { Control = 1 }
    enum SnapDef { Control = 1 }

    // Where MSFS keeps the module's work folder (2020 / 2024, MS Store / Steam).
    static readonly string[] SnapshotCandidates =
    {
        @"%LOCALAPPDATA%\Packages\Microsoft.FlightSimulator_8wekyb3d8bbwe\LocalState\packages\ehslink-module\work\snapshot.txt",
        @"%APPDATA%\Microsoft Flight Simulator\Packages\ehslink-module\work\snapshot.txt",
        // MSFS 2024 runs our 2020-SDK module under WASM\MSFS2020 (confirmed on MS Store 2024)
        @"%LOCALAPPDATA%\Packages\Microsoft.Limitless_8wekyb3d8bbwe\LocalState\WASM\MSFS2020\ehslink-module\work\snapshot.txt",
        @"%APPDATA%\Microsoft Flight Simulator 2024\WASM\MSFS2020\ehslink-module\work\snapshot.txt",
        @"%LOCALAPPDATA%\Packages\Microsoft.Limitless_8wekyb3d8bbwe\LocalState\WASM\MSFS2024\ehslink-module\work\snapshot.txt",
        @"%APPDATA%\Microsoft Flight Simulator 2024\WASM\MSFS2024\ehslink-module\work\snapshot.txt",
    };

    static DateTime NewestSnapshot(out string path)
    {
        path = Environment.ExpandEnvironmentVariables(SnapshotCandidates[0]);
        var newest = DateTime.MinValue;
        foreach (var c in SnapshotCandidates)
        {
            var p = Environment.ExpandEnvironmentVariables(c);
            if (!System.IO.File.Exists(p)) continue;
            var t = System.IO.File.GetLastWriteTimeUtc(p);
            if (t > newest) { newest = t; path = p; }
        }
        return newest;
    }

    // Asks the MSFS module for a variable snapshot, waits for it, then lists the variables that
    // hold any of the given values (e.g. the numbers dialled on the FCU).
    static int Snapshot(string[] args)
    {
        string path;
        var before = NewestSnapshot(out path);
        var evt = new AutoResetEvent(false);
        using (var sc = new Microsoft.FlightSimulator.SimConnect.SimConnect("EHS Link tool", IntPtr.Zero, 0, evt, 0))
        {
            sc.MapClientDataNameToID("EHSLink.Control", SnapCd.Control);
            sc.AddToClientDataDefinition(SnapDef.Control, 0, 4, 0, 0);
            sc.SetClientData(SnapCd.Control, SnapDef.Control, Microsoft.FlightSimulator.SimConnect.SIMCONNECT_CLIENT_DATA_SET_FLAG.DEFAULT, 0,
                             new ControlRequest { counter = Environment.TickCount | 1 });
            for (int i = 0; i < 30; i++)
            {
                evt.WaitOne(500);
                try { sc.ReceiveMessage(); } catch { }
                if (NewestSnapshot(out path) > before) break;
            }
        }
        if (NewestSnapshot(out path) <= before)
        {
            Console.WriteLine("No new snapshot (is the updated EHS Link module loaded? It needs an MSFS restart after updating).");
            return 1;
        }
        Thread.Sleep(300);   // let the module finish writing
        var lines = System.IO.File.ReadAllLines(path);
        Console.WriteLine("Snapshot: " + path + " (" + lines.Length + " lines)");
        foreach (var l in lines) if (l.StartsWith("title") || l.StartsWith("path") || l.StartsWith("profile") || l.StartsWith("# ")) Console.WriteLine("  " + l);
        var targets = new System.Collections.Generic.List<double>();
        for (int i = 1; i < args.Length; i++) targets.Add(double.Parse(args[i], System.Globalization.CultureInfo.InvariantCulture));
        if (targets.Count == 0) return 0;
        Console.WriteLine("Variables holding " + string.Join(", ", targets) + ":");
        foreach (var l in lines)
        {
            int eq = l.LastIndexOf(" = ");
            if (eq < 0 || !(l.StartsWith("L:") || l.StartsWith("A:"))) continue;
            double v;
            if (!double.TryParse(l.Substring(eq + 3), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out v)) continue;
            foreach (var t in targets) if (Math.Abs(v - t) < 0.6) { Console.WriteLine("  " + l); break; }
        }
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
