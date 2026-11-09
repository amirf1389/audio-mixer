import React, { useState } from 'react';
import { ActivityIndicator, Text, TextInput, View } from 'react-native';
import { DaemonClient, type Endpoint } from '../api/client';
import { Btn } from '../ui/Btn';
import { T } from '../ui/theme';

// Host, port and token of the PC running amixos-daemon (the token is in ~/.amixos/daemon.token on that PC; the desktop console shows it under SETTINGS).
export function Connect({ initial, onConnected }: { initial: Endpoint; onConnected: (c: DaemonClient, name: string) => void }) {
  const [ep, setEp] = useState<Endpoint>(initial); const [busy, setBusy] = useState(false); const [err, setErr] = useState('');
  const go = async () => {
    setBusy(true); setErr('');
    try { const c = new DaemonClient(ep); const s = await c.status(); await c.state(); onConnected(c, `${s.name} ${s.version}`); }
    catch (e) { setErr(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };
  const field = (label: string, value: string, set: (v: string) => void, secure = false, keyboard: 'default' | 'numeric' = 'default') => (
    <View style={{ marginBottom: 12 }}>
      <Text style={{ color: T.dim, fontSize: 12, marginBottom: 4 }}>{label}</Text>
      <TextInput value={value} onChangeText={set} secureTextEntry={secure} keyboardType={keyboard} autoCapitalize="none" autoCorrect={false}
        style={{ color: T.text, backgroundColor: T.panel, borderWidth: 1, borderColor: T.line, borderRadius: 8, paddingHorizontal: 12, paddingVertical: 10, fontSize: 15 }} />
    </View>
  );
  return (
    <View style={{ flex: 1, backgroundColor: T.bg, padding: 20, justifyContent: 'center' }}>
      <Text style={{ color: T.text, fontSize: 26, fontWeight: '700', marginBottom: 4 }}>A mix OS</Text>
      <Text style={{ color: T.dim, marginBottom: 24 }}>Remote for the engine running on your PC (amixos-daemon).</Text>
      {field('PC address', ep.host, host => setEp({ ...ep, host }))}
      {field('Port', String(ep.port), p => setEp({ ...ep, port: Number(p) || 8765 }), false, 'numeric')}
      {field('Token (~/.amixos/daemon.token on the PC)', ep.token, token => setEp({ ...ep, token }), true)}
      <View style={{ flexDirection: 'row', alignItems: 'center', marginBottom: 12 }}>
        <Btn title={ep.secure ? 'HTTPS (nginx front)' : 'Plain HTTP (LAN only)'} on={!!ep.secure} onPress={() => setEp({ ...ep, secure: !ep.secure })} small />
      </View>
      {!ep.secure ? <Text style={{ color: T.warn, fontSize: 12, marginBottom: 12 }}>Plain HTTP is not encrypted: use it only on a trusted home or studio network. For anything else, run the daemon behind the nginx TLS front (deploy/nginx) and switch to HTTPS.</Text> : null}
      {err ? <Text style={{ color: T.clip, marginBottom: 12 }}>{err}</Text> : null}
      {busy ? <ActivityIndicator color={T.accent} /> : <Btn title="Connect" on onPress={go} />}
    </View>
  );
}
