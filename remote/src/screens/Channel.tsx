import React, { useState } from 'react';
import { ScrollView, Text, TextInput, View } from 'react-native';
import { DaemonClient, fmtDb } from '../api/client';
import type { FilterType } from '../api/types';
import { Btn } from '../ui/Btn';
import { T } from '../ui/theme';

// One strip: name, pan, polarity, delay, a 4-band EQ, compressor and gate. The phone keeps these values (the daemon has no readback for the channel DSP yet).
const Row = ({ label, children }: { label: string; children: React.ReactNode }) => (
  <View style={{ flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', paddingVertical: 8, borderBottomWidth: 1, borderColor: T.line }}>
    <Text style={{ color: T.dim, width: 90 }}>{label}</Text><View style={{ flexDirection: 'row', gap: 6, flex: 1, justifyContent: 'flex-end', alignItems: 'center' }}>{children}</View>
  </View>
);
const Step = ({ value, fmt, onStep }: { value: number; fmt: (v: number) => string; onStep: (d: number) => void }) => (
  <View style={{ flexDirection: 'row', alignItems: 'center', gap: 6 }}><Btn title="−" small onPress={() => onStep(-1)} /><Text style={{ color: T.text, minWidth: 64, textAlign: 'center', fontVariant: ['tabular-nums'] }}>{fmt(value)}</Text><Btn title="+" small onPress={() => onStep(1)} /></View>
);
const types: FilterType[] = ['peaking', 'lowshelf', 'highshelf', 'highpass', 'lowpass', 'notch'];

export function Channel({ client, strip, name, onBack, onError }: { client: DaemonClient; strip: number; name: string; onBack: () => void; onError: (e: string) => void }) {
  const [nm, setNm] = useState(name); const [pan, setPan] = useState(0); const [pol, setPol] = useState(false); const [delay, setDelay] = useState(0);
  const [eq, setEq] = useState(Array.from({ length: 4 }, (_, b) => ({ type: (b === 0 ? 'highpass' : b === 3 ? 'highshelf' : 'peaking') as FilterType, freq: [80, 250, 2500, 8000][b], q: 0.9, gainDb: 0, on: false })));
  const [comp, setComp] = useState({ on: false, thresholdDb: -18, ratio: 3, attackMs: 10, releaseMs: 100, makeupDb: 0 });
  const [gate, setGate] = useState({ on: false, thresholdDb: -40, rangeDb: -80, attackMs: 1, holdMs: 50, releaseMs: 100 });
  const c = (p: Promise<unknown>) => p.catch(e => onError(String(e)));
  const sendEq = (b: number, next: typeof eq[number]) => { const e = eq.map((x, i) => (i === b ? next : x)); setEq(e); c(client.control({ op: 'eq', strip, band: b, type: next.on ? next.type : 'off', freq: next.freq, q: next.q, gainDb: next.gainDb })); };
  const sendComp = (next: typeof comp) => { setComp(next); c(client.control({ op: 'compressor', strip, ...next, kneeDb: 6 })); };
  const sendGate = (next: typeof gate) => { setGate(next); c(client.control({ op: 'gate', strip, ...next })); };
  return (
    <View style={{ flex: 1, backgroundColor: T.bg }}>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10, padding: 12, borderBottomWidth: 1, borderColor: T.line }}>
        <Btn title="‹ Mixer" small onPress={onBack} />
        <TextInput value={nm} onChangeText={setNm} onEndEditing={() => nm.trim() && c(client.control({ op: 'name', strip, name: nm.trim() }))} style={{ color: T.text, fontSize: 18, fontWeight: '700', flex: 1 }} />
        <Text style={{ color: T.dim }}>CH {strip + 1}</Text>
      </View>
      <ScrollView contentContainerStyle={{ padding: 12 }}>
        <Row label="Pan"><Step value={pan} fmt={v => (v === 0 ? 'C' : v < 0 ? `L${Math.round(-v * 100)}` : `R${Math.round(v * 100)}`)} onStep={d => { const v = Math.max(-1, Math.min(1, +(pan + d * 0.1).toFixed(2))); setPan(v); c(client.control({ op: 'pan', strip, pan: v })); }} /></Row>
        <Row label="Polarity"><Btn title="Ø" small on={pol} onPress={() => { setPol(!pol); c(client.control({ op: 'polarity', strip, on: !pol })); }} /></Row>
        <Row label="Delay"><Step value={delay} fmt={v => `${v.toFixed(1)} ms`} onStep={d => { const v = Math.max(0, Math.min(500, +(delay + d * 0.5).toFixed(1))); setDelay(v); c(client.control({ op: 'delay', strip, ms: v })); }} /></Row>
        <Text style={{ color: T.accent, fontWeight: '700', marginTop: 16, marginBottom: 4 }}>EQ</Text>
        {eq.map((b, i) => (
          <View key={i} style={{ backgroundColor: T.panel, borderRadius: 8, padding: 8, marginBottom: 6, borderWidth: 1, borderColor: b.on ? T.accent : T.line }}>
            <View style={{ flexDirection: 'row', alignItems: 'center', gap: 6, marginBottom: 6 }}>
              <Btn title={b.on ? 'ON' : 'OFF'} small on={b.on} onPress={() => sendEq(i, { ...b, on: !b.on })} />
              <Btn title={b.type} small onPress={() => sendEq(i, { ...b, type: types[(types.indexOf(b.type) + 1) % types.length] })} />
            </View>
            <View style={{ flexDirection: 'row', justifyContent: 'space-between', flexWrap: 'wrap', gap: 6 }}>
              <Step value={b.freq} fmt={v => (v >= 1000 ? `${(v / 1000).toFixed(2)} k` : `${Math.round(v)} Hz`)} onStep={d => sendEq(i, { ...b, freq: Math.max(20, Math.min(20000, Math.round(b.freq * (d > 0 ? 1.122 : 1 / 1.122)))) })} />
              <Step value={b.gainDb} fmt={v => `${fmtDb(v)} dB`} onStep={d => sendEq(i, { ...b, gainDb: Math.max(-18, Math.min(18, +(b.gainDb + d).toFixed(1))) })} />
              <Step value={b.q} fmt={v => `Q ${v.toFixed(2)}`} onStep={d => sendEq(i, { ...b, q: Math.max(0.1, Math.min(10, +(b.q * (d > 0 ? 1.25 : 0.8)).toFixed(2))) })} />
            </View>
          </View>
        ))}
        <Text style={{ color: T.accent, fontWeight: '700', marginTop: 16, marginBottom: 4 }}>COMPRESSOR</Text>
        <Row label="Comp"><Btn title={comp.on ? 'ON' : 'OFF'} small on={comp.on} onPress={() => sendComp({ ...comp, on: !comp.on })} /></Row>
        <Row label="Threshold"><Step value={comp.thresholdDb} fmt={v => `${fmtDb(v)} dB`} onStep={d => sendComp({ ...comp, thresholdDb: Math.max(-60, Math.min(0, comp.thresholdDb + d)) })} /></Row>
        <Row label="Ratio"><Step value={comp.ratio} fmt={v => `${v.toFixed(1)}:1`} onStep={d => sendComp({ ...comp, ratio: Math.max(1, Math.min(20, +(comp.ratio + d * 0.5).toFixed(1))) })} /></Row>
        <Row label="Attack"><Step value={comp.attackMs} fmt={v => `${v} ms`} onStep={d => sendComp({ ...comp, attackMs: Math.max(0.1, Math.min(200, +(comp.attackMs * (d > 0 ? 1.5 : 1 / 1.5)).toFixed(1))) })} /></Row>
        <Row label="Release"><Step value={comp.releaseMs} fmt={v => `${v} ms`} onStep={d => sendComp({ ...comp, releaseMs: Math.max(5, Math.min(2000, Math.round(comp.releaseMs * (d > 0 ? 1.5 : 1 / 1.5)))) })} /></Row>
        <Row label="Makeup"><Step value={comp.makeupDb} fmt={v => `${fmtDb(v)} dB`} onStep={d => sendComp({ ...comp, makeupDb: Math.max(0, Math.min(24, comp.makeupDb + d)) })} /></Row>
        <Text style={{ color: T.accent, fontWeight: '700', marginTop: 16, marginBottom: 4 }}>NOISE GATE</Text>
        <Row label="Gate"><Btn title={gate.on ? 'ON' : 'OFF'} small on={gate.on} onPress={() => sendGate({ ...gate, on: !gate.on })} /></Row>
        <Row label="Threshold"><Step value={gate.thresholdDb} fmt={v => `${fmtDb(v)} dB`} onStep={d => sendGate({ ...gate, thresholdDb: Math.max(-80, Math.min(0, gate.thresholdDb + d)) })} /></Row>
        <Row label="Range"><Step value={gate.rangeDb} fmt={v => `${fmtDb(v)} dB`} onStep={d => sendGate({ ...gate, rangeDb: Math.max(-120, Math.min(0, gate.rangeDb + d * 5)) })} /></Row>
        <Row label="Hold"><Step value={gate.holdMs} fmt={v => `${v} ms`} onStep={d => sendGate({ ...gate, holdMs: Math.max(0, Math.min(2000, gate.holdMs + d * 10)) })} /></Row>
        <Row label="Release"><Step value={gate.releaseMs} fmt={v => `${v} ms`} onStep={d => sendGate({ ...gate, releaseMs: Math.max(5, Math.min(2000, Math.round(gate.releaseMs * (d > 0 ? 1.5 : 1 / 1.5)))) })} /></Row>
      </ScrollView>
    </View>
  );
}
