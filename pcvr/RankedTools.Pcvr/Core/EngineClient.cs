using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace RankedTools.Pcvr.Core;

internal sealed class EngineClient
{
    private const string Library = "RankedTools.Core.dll";
    private static readonly UTF8Encoding Utf8 = new(false, true);
    private bool initialized;

    [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr LoadLibraryW(string path);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int rt_abi_version();
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int rt_call(byte[] request, UIntPtr length, out IntPtr result, out UIntPtr size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int rt_chart(int service, out IntPtr result, out UIntPtr size, out int width, out int height);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern void rt_free(IntPtr buffer);

    public void Initialize(string nativePath, string dataDirectory, string playlistDirectory)
    {
        if (initialized) return;
        if (!Environment.Is64BitProcess || Environment.OSVersion.Platform != PlatformID.Win32NT)
            throw new PlatformNotSupportedException("This RankedTools preview requires Windows x64.");
        nativePath = Path.GetFullPath(nativePath);
        if (!File.Exists(nativePath)) throw new FileNotFoundException("Extract the complete RankedTools PCVR ZIP into your game folder.", nativePath);
        if (LoadLibraryW(nativePath) == IntPtr.Zero)
            throw new InvalidOperationException("Could not load the RankedTools engine (Windows error " + Marshal.GetLastWin32Error() + ").");
        if (rt_abi_version() != 1) throw new InvalidOperationException("RankedTools plugin and native engine versions do not match.");
        Invoke(new JObject { ["command"] = "initialize", ["dataDirectory"] = Path.GetFullPath(dataDirectory), ["playlistDirectory"] = Path.GetFullPath(playlistDirectory) });
        initialized = true;
    }

    public JObject Invoke(JObject request)
    {
        var bytes = Utf8.GetBytes(request.ToString(Formatting.None));
        var success = rt_call(bytes, (UIntPtr)(uint)bytes.Length, out var result, out var size);
        try
        {
            if (result == IntPtr.Zero) throw new InvalidOperationException("The RankedTools engine could not allocate a response.");
            var count = checked((int)size.ToUInt64());
            var buffer = new byte[count]; Marshal.Copy(result, buffer, 0, count);
            var response = JObject.Parse(Utf8.GetString(buffer));
            if (success == 0 || response.Value<bool?>("ok") != true)
                throw new InvalidOperationException(response.Value<string>("error") ?? "Unknown engine error.");
            return response;
        }
        finally { if (result != IntPtr.Zero) rt_free(result); }
    }

    public byte[]? Chart(int service, out int width, out int height)
    {
        var success = rt_chart(service, out var result, out var size, out width, out height);
        try
        {
            if (success == 0 || result == IntPtr.Zero) return null;
            var length = checked((int)size.ToUInt64());
            if (width <= 0 || height <= 0 || length != checked(width * height * 4))
                throw new InvalidOperationException("Invalid ability-chart image.");
            var bytes = new byte[length]; Marshal.Copy(result, bytes, 0, length); return bytes;
        }
        finally { if (result != IntPtr.Zero) rt_free(result); }
    }

    public void Shutdown() { if (initialized) Invoke(new JObject { ["command"] = "shutdown" }); }
}
