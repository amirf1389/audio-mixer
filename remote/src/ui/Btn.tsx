import React from 'react';
import { Pressable, Text } from 'react-native';
import { T } from './theme';

export function Btn({ title, on, color, onPress, small }: { title: string; on?: boolean; color?: string; onPress: () => void; small?: boolean }) {
  const c = color ?? T.accent;
  return (
    <Pressable onPress={onPress} style={({ pressed }) => ({ paddingHorizontal: small ? 8 : 14, paddingVertical: small ? 4 : 10, borderRadius: 6, borderWidth: 1, borderColor: on ? c : T.line, backgroundColor: on ? c : pressed ? '#1b2130' : T.panel })}>
      <Text style={{ color: on ? '#0b0d12' : T.text, fontWeight: '600', fontSize: small ? 11 : 13 }}>{title}</Text>
    </Pressable>
  );
}
