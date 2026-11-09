import React, { useEffect, useRef, useState } from 'react';
import { ScrollView, Text, View } from 'react-native';
import { DaemonClient } from '../api/client';
import type { ConsoleState, Meters } from '../api/types';
import { Btn } from '../ui/Btn';
import { Fader } from '../ui/Fader';
import { Meter } from '../ui/Meter';
import { T } from '../ui/theme';

// Strips with meters, mute / solo, a master fader; a tap on a name opens the channel page.
export function Mixer({ client, onChannel }: { client: DaemonClient; onChannel: (strip: number) => void }) {
  const [state, setState] = useState<ConsoleState | null>(null); const [meters, setMeters] = useState<Meters | null>(null); const [link, setLink] = useState(''); const pending = useRef<Map<string, ReturnType<typeof setTimeout>>>(new Map());
  useEffect(() => { let live = true; const load = () => client.state().then(s => live && setState(s)).catch(e => setLink(String(e))); load(); const t = setInterval(load, 3000); return () => { live = false; clearInterval(t); }; }, [client]);
  useEffect(() => client.meterStream(setMeters, (ok, e) => setLink(ok ? '' : e ?? 'reconnecting…')), [client]);
  // faders fire many times while dragging: show at once, send at most every 40 ms per control
  const send = (key: string, local: (s: ConsoleState) => ConsoleState, cmd: () => Promise<unknown>) => {
    setState(s => (s ? local(s) : s));
    const old = pending.current.get(key); if (old) clearTimeout(old);
    pending.current.set(key, setTimeout(() => { pending.current.delete(key); cmd().catch(e => setLink(String(e))); }, 40));
  };
  if (!state) return <View style={{ flex: 1, backgroundColor: T.bg, justifyContent: 'center' }}><Text style={{ color: T.dim, textAlign: 'center' }}>{link || 'Loading the console…'}</Text></View>;
  const m = meters?.strips ?? [];
  return (
    <View style={{ flex: 1, backgroundColor: T.bg }}>
      <View style={{ flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center', paddingHorizontal: 12, paddingVertical: 8, borderBottomWidth: 1, borderColor: T.line }}>
        <Text style={{ color: T.text, fontWeight: '700' }}>MIXER  <Text style={{ color: T.dim, fontWeight: '400' }}>{state.backend || 'no device'}{meters?.xruns ? `  xruns ${meters.xruns}` : ''}</Text></Text>
        <Text style={{ color: link ? T.warn : T.ok, fontSize: 12 }}>{link || 'live'}</Text>
      </View>
      <ScrollView horizontal contentContainerStyle={{ padding: 12, gap: 10 }}>
        {state.strips.map((s, i) => (
          <View key={s.id} style={{ width: 78, backgroundColor: T.panel, borderRadius: T.radius, padding: 6, alignItems: 'center', borderWidth: 1, borderColor: T.line }}>
            <Btn title={s.name} small onPress={() => onChannel(i)} />
            <View style={{ flexDirection: 'row', gap: 6, marginTop: 8 }}>
              <Meter pk={m[i]?.pk ?? 0} rms={m[i]?.rms ?? 0} clip={m[i]?.clip} />
              <Fader db={s.db} onChange={db => send(`g${i}`, st => ({ ...st, strips: st.strips.map(x => (x.id === s.id ? { ...x, db } : x)) }), () => client.control({ op: 'gain', strip: i, db }))} height={160} />
            </View>
            {m[i]?.gr && m[i].gr < -0.5 ? <Text style={{ color: T.warn, fontSize: 10 }}>GR {m[i].gr.toFixed(1)}</Text> : <Text style={{ color: T.dim, fontSize: 10 }}>pan {s.pan.toFixed(2)}</Text>}
            <View style={{ flexDirection: 'row', gap: 4, marginTop: 6 }}>
              <Btn title="M" small on={s.mute} color={T.mute} onPress={() => send(`m${i}`, st => ({ ...st, strips: st.strips.map(x => (x.id === s.id ? { ...x, mute: !s.mute } : x)) }), () => client.control({ op: 'mute', strip: i, on: !s.mute }))} />
              <Btn title="S" small on={s.solo} color={T.solo} onPress={() => send(`s${i}`, st => ({ ...st, strips: st.strips.map(x => (x.id === s.id ? { ...x, solo: !s.solo } : x)) }), () => client.control({ op: 'solo', strip: i, on: !s.solo }))} />
            </View>
          </View>
        ))}
        <View style={{ width: 110, backgroundColor: '#171c2a', borderRadius: T.radius, padding: 6, alignItems: 'center', borderWidth: 1, borderColor: T.accent }}>
          <Text style={{ color: T.text, fontWeight: '700', fontSize: 12 }}>MASTER</Text>
          <View style={{ flexDirection: 'row', gap: 6, marginTop: 8 }}>
            <Meter pk={meters?.master?.l ?? 0} rms={meters?.master?.rms ?? 0} clip={meters?.master?.clip} />
            <Meter pk={meters?.master?.r ?? 0} rms={meters?.master?.rms ?? 0} clip={meters?.master?.clip} />
            <Fader db={state.master.db} onChange={db => send('master', st => ({ ...st, master: { ...st.master, db } }), () => client.control({ op: 'master', db }))} height={160} />
          </View>
          <Text style={{ color: T.dim, fontSize: 10, marginTop: 4 }}>{meters?.loudness ? `${meters.loudness.s.toFixed(1)} LUFS-S` : ''}</Text>
          <View style={{ flexDirection: 'row', gap: 4, marginTop: 6 }}>
            <Btn title="LIM" small on={state.master.limiter} onPress={() => send('lim', st => ({ ...st, master: { ...st.master, limiter: !st.master.limiter } }), () => client.control({ op: 'limiter', on: !state.master.limiter, ceilingDb: state.master.ceilingDb }))} />
            <Btn title="CLR" small onPress={() => client.control({ op: 'clearClips' }).catch(e => setLink(String(e)))} />
          </View>
        </View>
      </ScrollView>
    </View>
  );
}
