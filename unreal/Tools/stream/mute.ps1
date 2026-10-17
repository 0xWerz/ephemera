# Mutes one program in Windows' volume mixer (what the speaker icon's "Volume mixer" does by hand).
#   mute.ps1 -ProcessId <id> [-Unmute]
# Used by stream.ps1: the game's sound still goes out to browsers (the stream takes it inside the
# game, before Windows ever plays it) while this PC's speakers stay quiet. A program only appears
# in the mixer once it has opened its sound, so this waits up to a minute for it.
param([Parameter(Mandatory = $true)][int]$ProcessId, [switch]$Unmute, [int]$WaitSeconds = 60)
$source = @'
using System;
using System.Runtime.InteropServices;
namespace EphemeraStream
{
    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
    class DeviceEnumerator { }

    [Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDeviceEnumerator
    {
        [PreserveSig] int EnumAudioEndpoints(int dataFlow, int stateMask, out IntPtr devices);
        [PreserveSig] int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice device);
    }

    [Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDevice
    {
        [PreserveSig] int Activate(ref Guid iid, int context, IntPtr activationParams, [MarshalAs(UnmanagedType.IUnknown)] out object instance);
    }

    [Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioSessionManager2
    {
        [PreserveSig] int GetAudioSessionControl(IntPtr sessionGuid, int flags, out IntPtr control);
        [PreserveSig] int GetSimpleAudioVolume(IntPtr sessionGuid, int flags, out IntPtr volume);
        [PreserveSig] int GetSessionEnumerator(out IAudioSessionEnumerator sessions);
    }

    [Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioSessionEnumerator
    {
        [PreserveSig] int GetCount(out int count);
        [PreserveSig] int GetSession(int index, out IAudioSessionControl2 session);
    }

    [Guid("BFB7FF88-7239-4FC9-8FA2-07C950BE9C6D"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioSessionControl2
    {
        [PreserveSig] int GetState(out int state);
        [PreserveSig] int GetDisplayName(out IntPtr name);
        [PreserveSig] int SetDisplayName([MarshalAs(UnmanagedType.LPWStr)] string name, ref Guid context);
        [PreserveSig] int GetIconPath(out IntPtr path);
        [PreserveSig] int SetIconPath([MarshalAs(UnmanagedType.LPWStr)] string path, ref Guid context);
        [PreserveSig] int GetGroupingParam(out Guid group);
        [PreserveSig] int SetGroupingParam(ref Guid group, ref Guid context);
        [PreserveSig] int RegisterAudioSessionNotification(IntPtr client);
        [PreserveSig] int UnregisterAudioSessionNotification(IntPtr client);
        [PreserveSig] int GetSessionIdentifier(out IntPtr id);
        [PreserveSig] int GetSessionInstanceIdentifier(out IntPtr id);
        [PreserveSig] int GetProcessId(out uint processId);
    }

    [Guid("87CE5498-68D6-44E5-9215-6DA47EF883D8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface ISimpleAudioVolume
    {
        [PreserveSig] int SetMasterVolume(float level, ref Guid context);
        [PreserveSig] int GetMasterVolume(out float level);
        [PreserveSig] int SetMute([MarshalAs(UnmanagedType.Bool)] bool mute, ref Guid context);
        [PreserveSig] int GetMute([MarshalAs(UnmanagedType.Bool)] out bool mute);
    }

    public static class Mixer
    {
        /// How many of the program's sound sessions were found and set.
        public static int Mute(uint processId, bool mute)
        {
            IMMDeviceEnumerator enumerator = (IMMDeviceEnumerator)(new DeviceEnumerator());
            IMMDevice device;
            if (enumerator.GetDefaultAudioEndpoint(0, 1, out device) != 0 || device == null) { return -1; }
            Guid iid = typeof(IAudioSessionManager2).GUID;
            object manager;
            if (device.Activate(ref iid, 23, IntPtr.Zero, out manager) != 0) { return -1; }
            IAudioSessionEnumerator sessions;
            if (((IAudioSessionManager2)manager).GetSessionEnumerator(out sessions) != 0) { return -1; }
            int count;
            sessions.GetCount(out count);
            int done = 0;
            for (int index = 0; index < count; index++)
            {
                IAudioSessionControl2 session;
                if (sessions.GetSession(index, out session) != 0 || session == null) { continue; }
                uint owner;
                session.GetProcessId(out owner);
                if (owner == processId)
                {
                    Guid none = Guid.Empty;
                    ((ISimpleAudioVolume)session).SetMute(mute, ref none);
                    done++;
                }
            }
            return done;
        }
    }
}
'@
Add-Type -TypeDefinition $source
$until = (Get-Date).AddSeconds($WaitSeconds)
$done = 0
do {
    $done = [EphemeraStream.Mixer]::Mute([uint32]$ProcessId, -not $Unmute)
    if ($done -le 0) { Start-Sleep -Milliseconds 500 }
} while ($done -le 0 -and (Get-Date) -lt $until -and (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue))
Write-Output ("{0} sound session(s) of process {1} {2}" -f [Math]::Max($done, 0), $ProcessId, $(if ($Unmute) { 'unmuted' } else { 'muted' }))
