import React, { useEffect, useState } from 'react';
import { ScrollView, Text, View } from 'react-native';
import { DaemonClient } from '../api/client';
import type { Device, Status } from '../api/types';
import { Btn } from '../ui/Btn';
import { T } from '../ui/theme';

// Status of the PC engine and the devices it can open; a tap opens that device (the daemon replays the console onto it).
export function Devices({ client, onBack, onError }: { client: DaemonClient; onBack: () => void; onError: (e: string) => void }) {
  const [st, setSt] = useState<Status | null>(null); const [devs, setDevs] = useState<Device[]>([]);
  const refresh = () => { client.status().then(setSt).catch(e => onError(String(e))); client.devices().then(setDevs).catch(e => onError(String(e))); };
  useEffect(refresh, [client]);
  const open = (d: Device) => client.control({ op: 'open', backend: d.backend, in: d.in ? d.id : '-', out: d.out ? d.id : '-', rate: d.defaultRate || 48000, frames: 256, inChannels: Math.min(d.in, 32), outChannels: Math.min(Math.max(d.out, 2), 32) }).then(refresh).catch(e => onError(String(e)));
  return (
    <View style={{ flex: 1, backgroundColor: T.bg }}>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10, padding: 12, borderBottomWidth: 1, borderColor: T.line }}>
        <Btn title="‹ Mixer" small onPress={onBack} /><Text style={{ color: T.text, fontSize: 18, fontWeight: '700', flex: 1 }}>DEVICES</Text><Btn title="Refresh" small onPress={refresh} />
      </View>
      <ScrollView contentContainerStyle={{ padding: 12 }}>
        {st ? <View style={{ backgroundColor: T.panel, borderRadius: 8, padding: 10, marginBottom: 12, borderWidth: 1, borderColor: T.line }}>
          <Text style={{ color: T.text, fontWeight: '700' }}>{st.name} {st.version}  <Text style={{ color: st.running ? T.ok : T.dim, fontWeight: '400' }}>{st.running ? `running on ${st.backend}` : 'no device open'}</Text></Text>
          <Text style={{ color: T.dim, fontSize: 12, marginTop: 4 }}>blocks {st.blocks}  xruns {st.xruns}  up {Math.round(st.uptimeS)} s  backends {st.backends.join(', ')}</Text>
          {st.lastError ? <Text style={{ color: T.clip, fontSize: 12, marginTop: 4 }}>{st.lastError}</Text> : null}
          <View style={{ flexDirection: 'row', gap: 6, marginTop: 8 }}><Btn title="Stop engine" small onPress={() => client.control({ op: 'stop' }).then(refresh).catch(e => onError(String(e)))} /></View>
        </View> : null}
        {devs.map(d => (
          <View key={d.backend + d.id} style={{ backgroundColor: T.panel, borderRadius: 8, padding: 10, marginBottom: 6, borderWidth: 1, borderColor: T.line, flexDirection: 'row', alignItems: 'center' }}>
            <View style={{ flex: 1 }}>
              <Text style={{ color: T.text }}>{d.name}</Text>
              <Text style={{ color: T.dim, fontSize: 12 }}>{d.backend.toUpperCase()}  in {d.in}  out {d.out}  {d.defaultRate ? `${d.defaultRate / 1000} kHz` : ''}{d.defaultOut ? '  default out' : ''}{d.defaultIn ? '  default in' : ''}</Text>
            </View>
            <Btn title="Open" small onPress={() => open(d)} />
          </View>
        ))}
      </ScrollView>
    </View>
  );
}
