// Reads the EHS Link MSFS module's output (SimConnect client data "EHSLink.Selections")
// using vPilot's own managed SimConnect library. Runs its own SimConnect connection on a
// background thread and reconnects when the sim restarts.

using System;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Microsoft.FlightSimulator.SimConnect;

namespace EhsLink
{
    internal sealed class Selections
    {
        public string Source;
        public bool Verified;
        public double Lat, Lon;
        public int? Alt, Hdg, Ias, Vs;
        public double? Mach;
        public bool Trk;
        public double? Baro;        // altimeter setting, hPa (1013.2 when STD)
        public string BaroUnit;     // unit shown to the pilot: "hPa", "inHg", or null if unknown

        /// <summary>The values that matter for change detection (no position, no time).</summary>
        public string Key()
        {
            return string.Join("|", Source, Alt, Hdg, Trk, Ias, Mach?.ToString("0.00", CultureInfo.InvariantCulture), Vs,
                               Baro?.ToString("0.0", CultureInfo.InvariantCulture), BaroUnit);
        }

        public string ToJson(string callsign, long unixTime)
        {
            var sb = new StringBuilder(240);
            sb.Append("{\"v\":1,\"cs\":\"").Append(callsign).Append("\",\"t\":").Append(unixTime);
            sb.Append(",\"lat\":").Append(Lat.ToString("0.0000", CultureInfo.InvariantCulture));
            sb.Append(",\"lon\":").Append(Lon.ToString("0.0000", CultureInfo.InvariantCulture));
            sb.Append(",\"src\":\"").Append(Escape(Source)).Append('"');
            sb.Append(",\"ver\":").Append(Verified ? "true" : "false");
            sb.Append(",\"alt\":").Append(N(Alt));
            sb.Append(",\"hdg\":").Append(N(Hdg));
            sb.Append(",\"trk\":").Append(Trk ? "true" : "false");
            sb.Append(",\"ias\":").Append(N(Ias));
            sb.Append(",\"mach\":").Append(Mach.HasValue ? Mach.Value.ToString("0.00", CultureInfo.InvariantCulture) : "null");
            sb.Append(",\"vs\":").Append(N(Vs));
            sb.Append(",\"baro\":").Append(Baro.HasValue ? Baro.Value.ToString("0.0", CultureInfo.InvariantCulture) : "null");
            sb.Append(",\"baroUnit\":").Append(BaroUnit == null ? "null" : "\"" + BaroUnit + "\"");
            sb.Append('}');
            return sb.ToString();
        }

        private static string N(int? v) => v.HasValue ? v.Value.ToString(CultureInfo.InvariantCulture) : "null";
        private static string Escape(string s) => (s ?? "").Replace("\\", "").Replace("\"", "");
    }

    internal enum ReadStatus { Ok, NoSim, NoModule, NoProfiles, NoProfile, PmdgBroadcastOff, NoApData, SimConnectUnavailable }

    internal sealed class ModuleReader : IDisposable
    {
        private const string AreaName = "EHSLink.Selections";
        private const uint AreaSize = 184;   // sizeof(EhsShared) in the module
        private enum Cd { Out = 1 }
        private enum Def { Out = 1 }
        private enum Req { Out = 1 }

        [StructLayout(LayoutKind.Sequential, Pack = 1, CharSet = CharSet.Ansi)]
        private struct EhsShared
        {
            public int version, sequence, status, flags, alt, hdg, ias, vs;
            public float mach, baro;
            public double lat, lon;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string source;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 96)] public string title;
        }

        private const int FAlt = 1, FHdg = 2, FIas = 4, FMach = 8, FVs = 16, FBaro = 32, FTrk = 64,
                          FBaroUnitKnown = 128, FBaroHpa = 256, FVerified = 512;

        private readonly object _lock = new object();
        private readonly AutoResetEvent _event = new AutoResetEvent(false);
        private Thread _thread;
        private volatile bool _stop;
        private volatile bool _simConnected;
        private DateTime _connectedAt, _lastData;
        private EhsShared _latest;
        private bool _haveData;

        public string LastTitle { get; private set; }

        public void Start()
        {
            if (_thread != null) return;
            _thread = new Thread(Run) { IsBackground = true, Name = "EHS Link SimConnect" };
            _thread.Start();
        }

        public ReadStatus Read(out Selections sel)
        {
            sel = null;
            if (FatalError != null) return ReadStatus.SimConnectUnavailable;
            EhsShared d;
            lock (_lock)
            {
                if (!_simConnected) return ReadStatus.NoSim;
                // The module writes at least once a second; nothing for 5 s = not installed / not running.
                if (!_haveData || (DateTime.UtcNow - _lastData).TotalSeconds > 5)
                    return (DateTime.UtcNow - _connectedAt).TotalSeconds > 5 ? ReadStatus.NoModule : ReadStatus.NoSim;
                d = _latest;
            }
            LastTitle = d.title;
            switch (d.status)
            {
                case 1: return ReadStatus.NoProfiles;
                case 2: return ReadStatus.NoProfile;
                case 3: return ReadStatus.PmdgBroadcastOff;
                case 4: return ReadStatus.NoApData;
            }
            sel = new Selections
            {
                Source = d.source,
                Verified = (d.flags & FVerified) != 0,
                Lat = d.lat,
                Lon = d.lon,
                Alt = (d.flags & FAlt) != 0 ? d.alt : (int?)null,
                Hdg = (d.flags & FHdg) != 0 ? d.hdg : (int?)null,
                Ias = (d.flags & FIas) != 0 ? d.ias : (int?)null,
                Mach = (d.flags & FMach) != 0 ? Math.Round(d.mach, 2) : (double?)null,
                Vs = (d.flags & FVs) != 0 ? d.vs : (int?)null,
                Trk = (d.flags & FTrk) != 0,
                Baro = (d.flags & FBaro) != 0 ? Math.Round(d.baro, 1) : (double?)null,
                BaroUnit = (d.flags & FBaroUnitKnown) != 0 ? ((d.flags & FBaroHpa) != 0 ? "hPa" : "inHg") : null,
            };
            return ReadStatus.Ok;
        }

        /// <summary>Set when SimConnect can't be used at all (e.g. the library failed to load).</summary>
        public string FatalError { get; private set; }

        private void Run()
        {
            // Everything that touches the SimConnect library lives in Session(): if the library
            // can't load, the exception surfaces here instead of killing the host (vPilot).
            while (!_stop)
            {
                try { Session(); }
                catch (System.IO.FileNotFoundException ex) { FatalError = ex.Message; return; }
                catch (System.IO.FileLoadException ex) { FatalError = ex.Message; return; }
                catch (BadImageFormatException ex) { FatalError = ex.Message; return; }
                catch (TypeLoadException ex) { FatalError = ex.Message; return; }
                catch (Exception) { }
                for (int i = 0; i < 50 && !_stop; i++) Thread.Sleep(100);
            }
        }

        [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
        private void Session()
        {
            {
                SimConnect sc = null;
                try
                {
                    sc = new SimConnect("EHS Link vPilot", IntPtr.Zero, 0, _event, 0);
                    sc.OnRecvQuit += (s, e) => { _simConnected = false; };
                    sc.OnRecvClientData += (s, e) =>
                    {
                        if (e.dwRequestID != (uint)Req.Out || e.dwData == null || e.dwData.Length == 0) return;
                        var d = (EhsShared)e.dwData[0];
                        lock (_lock) { _latest = d; _haveData = true; _lastData = DateTime.UtcNow; }
                    };
                    sc.MapClientDataNameToID(AreaName, Cd.Out);
                    sc.AddToClientDataDefinition(Def.Out, 0, AreaSize, 0, 0);
                    sc.RegisterStruct<SIMCONNECT_RECV_CLIENT_DATA, EhsShared>(Def.Out);
                    sc.RequestClientData(Cd.Out, Req.Out, Def.Out, SIMCONNECT_CLIENT_DATA_PERIOD.ON_SET,
                                         SIMCONNECT_CLIENT_DATA_REQUEST_FLAG.DEFAULT, 0, 0, 0);
                    lock (_lock) { _simConnected = true; _connectedAt = DateTime.UtcNow; _haveData = false; }

                    while (!_stop && _simConnected)
                    {
                        _event.WaitOne(500);
                        sc.ReceiveMessage();
                    }
                }
                catch (System.Runtime.InteropServices.COMException)
                {
                    // Sim not running yet, or it closed: Run() retries.
                }
                finally
                {
                    lock (_lock) { _simConnected = false; }
                    try { sc?.Dispose(); } catch { }
                }
            }
        }

        public void Dispose()
        {
            _stop = true;
            _event.Set();
        }
    }
}
