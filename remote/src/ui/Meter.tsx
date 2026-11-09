import React from 'react';
import { View } from 'react-native';
import { T } from './theme';

// a vertical peak / RMS bar: pk and rms are linear 0..1+, drawn in dB over 60 dB
const toH = (lin: number) => { const db = lin <= 0 ? -120 : 20 * Math.log10(lin); return Math.max(0, Math.min(1, (db + 60) / 60)); };
export function Meter({ pk, rms, clip, height = 160, width = 10 }: { pk: number; rms: number; clip?: number; height?: number; width?: number }) {
  const hp = toH(pk), hr = toH(rms);
  const color = pk >= 1 ? T.clip : pk >= 0.5 ? T.warn : T.ok;
  return (
    <View style={{ width, height, backgroundColor: '#0a0c10', borderRadius: 3, overflow: 'hidden', justifyContent: 'flex-end', borderWidth: 1, borderColor: clip ? T.clip : T.line }}>
      <View style={{ position: 'absolute', bottom: 0, left: 0, right: 0, height: height * hp, backgroundColor: color, opacity: 0.45 }} />
      <View style={{ height: height * hr, backgroundColor: color }} />
    </View>
  );
}
