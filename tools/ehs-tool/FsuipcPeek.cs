// Diagnostic only (not part of the plugin): reads a few FSUIPC offsets to cross-check what the
// MSFS module reports, e.g. whether PMDG is broadcasting at all.
using System;
using System.Runtime.InteropServices;

static class FsuipcPeek
{
    [DllImport("user32.dll", CharSet = CharSet.Ansi)] static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string t);
    [DllImport("user32.dll", CharSet = CharSet.Ansi)] static extern uint RegisterWindowMessage(string n);
    [DllImport("user32.dll")] static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, IntPtr l, uint f, uint t, out IntPtr r);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)] static extern ushort GlobalAddAtom(string n);
    [DllImport("kernel32.dll")] static extern ushort GlobalDeleteAtom(ushort a);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)] static extern IntPtr CreateFileMapping(IntPtr f, IntPtr a, uint p, uint hi, uint lo, string n);
    [DllImport("kernel32.dll")] static extern IntPtr MapViewOfFile(IntPtr m, uint a, uint hi, uint lo, UIntPtr b);
    [DllImport("kernel32.dll")] static extern bool UnmapViewOfFile(IntPtr v);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);

    /// <summary>Reads L:vars live through FSUIPC (0x0D6C/0x0D70 into user offsets 0x66C0+); NaN if missing.</summary>
    public static double[] ReadLvars(params string[] names)
    {
        var hwnd = FindWindowEx(IntPtr.Zero, IntPtr.Zero, "UIPCMAIN", null);
        if (hwnd == IntPtr.Zero) return null;
        var name = "FsasmLib:IPC:" + System.Diagnostics.Process.GetCurrentProcess().Id.ToString("X") + ":2";
        var atom = GlobalAddAtom(name);
        var map = CreateFileMapping(new IntPtr(-1), IntPtr.Zero, 4, 0, 0x8000, name);
        var view = MapViewOfFile(map, 2, 0, 0, UIntPtr.Zero);
        try
        {
            var result = new double[names.Length];
            for (int start = 0; start < names.Length; start += 7)   // 7 user slots of 8 bytes per call
            {
                int pos = 0, n = Math.Min(7, names.Length - start);
                var at = new int[n];
                for (int i = 0; i < n; i++)
                {
                    int slot = 0x66C0 + 8 * i;
                    // sentinel NaN in the slot, so a missing L:var is detectable
                    pos = Write(view, pos, slot, BitConverter.GetBytes(double.NaN));
                    pos = Write(view, pos, 0x0D6C, BitConverter.GetBytes(slot));
                    var s = System.Text.Encoding.ASCII.GetBytes(":" + names[start + i] + "\0");
                    pos = Write(view, pos, 0x0D70, s);
                    Marshal.WriteInt32(view, pos, 1); Marshal.WriteInt32(view, pos + 4, slot);
                    Marshal.WriteInt32(view, pos + 8, 8); Marshal.WriteInt32(view, pos + 12, 0);
                    at[i] = pos + 16; pos += 16 + 8;
                }
                Marshal.WriteInt32(view, pos, 0);
                IntPtr r;
                if (SendMessageTimeout(hwnd, RegisterWindowMessage("FsasmLib:IPC"), new IntPtr(atom), IntPtr.Zero, 1, 2000, out r) == IntPtr.Zero || r.ToInt64() != 1) return null;
                for (int i = 0; i < n; i++)
                {
                    var b = new byte[8]; Marshal.Copy(view + at[i], b, 0, 8);
                    result[start + i] = BitConverter.ToDouble(b, 0);
                }
            }
            return result;
        }
        finally { UnmapViewOfFile(view); CloseHandle(map); GlobalDeleteAtom(atom); }
    }

    static int Write(IntPtr view, int pos, int offset, byte[] data)
    {
        Marshal.WriteInt32(view, pos, 2); Marshal.WriteInt32(view, pos + 4, offset); Marshal.WriteInt32(view, pos + 8, data.Length);
        Marshal.Copy(data, 0, view + pos + 12, data.Length);
        return pos + 12 + data.Length;
    }

    /// <summary>Reads the given (offset, size) pairs in one FSUIPC call; null if FSUIPC isn't reachable.</summary>
    public static byte[][] Read(params int[] offsetSizePairs)
    {
        var hwnd = FindWindowEx(IntPtr.Zero, IntPtr.Zero, "UIPCMAIN", null);
        if (hwnd == IntPtr.Zero) return null;
        var name = "FsasmLib:IPC:" + System.Diagnostics.Process.GetCurrentProcess().Id.ToString("X") + ":1";
        var atom = GlobalAddAtom(name);
        var map = CreateFileMapping(new IntPtr(-1), IntPtr.Zero, 4, 0, 0x8000, name);
        var view = MapViewOfFile(map, 2, 0, 0, UIntPtr.Zero);
        try
        {
            int pos = 0, n = offsetSizePairs.Length / 2;
            var at = new int[n];
            for (int i = 0; i < n; i++)
            {
                Marshal.WriteInt32(view, pos, 1); Marshal.WriteInt32(view, pos + 4, offsetSizePairs[2 * i]);
                Marshal.WriteInt32(view, pos + 8, offsetSizePairs[2 * i + 1]); Marshal.WriteInt32(view, pos + 12, 0);
                at[i] = pos + 16; pos += 16 + offsetSizePairs[2 * i + 1];
            }
            Marshal.WriteInt32(view, pos, 0);
            IntPtr r;
            if (SendMessageTimeout(hwnd, RegisterWindowMessage("FsasmLib:IPC"), new IntPtr(atom), IntPtr.Zero, 1, 2000, out r) == IntPtr.Zero || r.ToInt64() != 1) return null;
            var res = new byte[n][];
            for (int i = 0; i < n; i++) { res[i] = new byte[offsetSizePairs[2 * i + 1]]; Marshal.Copy(view + at[i], res[i], 0, res[i].Length); }
            return res;
        }
        finally { UnmapViewOfFile(view); CloseHandle(map); GlobalDeleteAtom(atom); }
    }
}
