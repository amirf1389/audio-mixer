// A mix OS Remote: Connect -> Mixer (-> Channel, Devices). The engine runs on the PC as amixos-daemon; this app only sends control and shows meters.
import React, { useState } from 'react';
import { SafeAreaView, StatusBar, Text, View } from 'react-native';
import { DaemonClient, type Endpoint } from './src/api/client';
import { Channel } from './src/screens/Channel';
import { Connect } from './src/screens/Connect';
import { Devices } from './src/screens/Devices';
import { Mixer } from './src/screens/Mixer';
import { Btn } from './src/ui/Btn';
import { T } from './src/ui/theme';

type Screen = { k: 'connect' } | { k: 'mixer' } | { k: 'channel'; strip: number; name: string } | { k: 'devices' };

export default function App() {
  const [client, setClient] = useState<DaemonClient | null>(null); const [screen, setScreen] = useState<Screen>({ k: 'connect' }); const [title, setTitle] = useState(''); const [err, setErr] = useState('');
  const initial: Endpoint = { host: '192.168.1.10', port: 8765, token: '' };
  return (
    <SafeAreaView style={{ flex: 1, backgroundColor: T.bg }}>
      <StatusBar barStyle="light-content" />
      {!client || screen.k === 'connect' ? <Connect initial={initial} onConnected={(c, name) => { setClient(c); setTitle(name); setScreen({ k: 'mixer' }); }} /> : null}
      {client && screen.k === 'mixer' ? <>
        <View style={{ flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', paddingHorizontal: 12, paddingTop: 8 }}>
          <Text style={{ color: T.dim, fontSize: 12 }}>{title} · {client.ep.host}</Text>
          <View style={{ flexDirection: 'row', gap: 6 }}><Btn title="Devices" small onPress={() => setScreen({ k: 'devices' })} /><Btn title="Disconnect" small onPress={() => { setClient(null); setScreen({ k: 'connect' }); }} /></View>
        </View>
        <Mixer client={client} onChannel={strip => client.state().then(s => setScreen({ k: 'channel', strip, name: s.strips[strip]?.name ?? `CH ${strip + 1}` })).catch(e => setErr(String(e)))} />
      </> : null}
      {client && screen.k === 'channel' ? <Channel client={client} strip={screen.strip} name={screen.name} onBack={() => setScreen({ k: 'mixer' })} onError={setErr} /> : null}
      {client && screen.k === 'devices' ? <Devices client={client} onBack={() => setScreen({ k: 'mixer' })} onError={setErr} /> : null}
      {err ? <View style={{ backgroundColor: '#3a1a1f', padding: 8, flexDirection: 'row', justifyContent: 'space-between' }}><Text style={{ color: T.clip, flex: 1 }}>{err}</Text><Btn title="×" small onPress={() => setErr('')} /></View> : null}
    </SafeAreaView>
  );
}
