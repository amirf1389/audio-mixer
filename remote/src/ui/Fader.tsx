import React, { useRef } from 'react';
import { PanResponder, Text, View } from 'react-native';
import { dbToPos, fmtDb, posToDb } from '../api/client';
import { T } from './theme';

// a touch fader: drag anywhere on the track; the value is sent while dragging (the daemon coalesces at block rate)
export function Fader({ db, onChange, height = 200, label }: { db: number; onChange: (db: number) => void; height?: number; label?: string }) {
  const h = useRef(height);
  const pan = useRef(PanResponder.create({
    onStartShouldSetPanResponder: () => true, onMoveShouldSetPanResponder: () => true,
    onPanResponderGrant: e => onChange(posToDb(1 - e.nativeEvent.locationY / h.current)),
    onPanResponderMove: e => onChange(posToDb(Math.max(0, Math.min(1, 1 - e.nativeEvent.locationY / h.current)))),
  })).current;
  const pos = dbToPos(db);
  return (
    <View style={{ alignItems: 'center' }}>
      <View {...pan.panHandlers} onLayout={e => { h.current = e.nativeEvent.layout.height; }} style={{ width: 36, height, backgroundColor: '#0a0c10', borderRadius: 6, borderWidth: 1, borderColor: T.line, justifyContent: 'flex-end' }}>
        <View style={{ position: 'absolute', left: 16, right: 16, top: 6, bottom: 6, backgroundColor: T.line, borderRadius: 2 }} />
        <View style={{ position: 'absolute', left: 2, right: 2, bottom: pos * (height - 28), height: 28, backgroundColor: '#2a3246', borderRadius: 4, borderWidth: 1, borderColor: T.accent }} />
      </View>
      <Text style={{ color: T.text, fontVariant: ['tabular-nums'], fontSize: 12, marginTop: 4 }}>{fmtDb(db)}</Text>
      {label ? <Text style={{ color: T.dim, fontSize: 11 }} numberOfLines={1}>{label}</Text> : null}
    </View>
  );
}
