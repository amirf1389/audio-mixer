use strict; use warnings; use Test::More tests => 9;
use Audio::AmixOS;
ok(length(Audio::AmixOS::version()) >= 5, 'version');
is(Audio::AmixOS::api_version(), 1, 'api version 1');
like(Audio::AmixOS::devices_json(), qr/"null"/, 'devices json lists the null device');
like(Audio::AmixOS::license_json(), qr/"state"/, 'licence json');
my $m = Audio::AmixOS::Mixer->new(4, 48000);
is($m->strips, 4, 'four strips');
my @tone = map { 0.5 * sin(2 * 3.14159265358979 * 1000 * $_ / 48000) } 0 .. 255;
$m->set_limiter(0, -0.5, 100); $m->set_gain(0, -6); $m->set_pan(0, 0);
my ($L, $R);
for (1 .. 20) { ($L, $R) = $m->process([\@tone], 256) }
my $pk = 0; for (@$L) { $pk = abs($_) if abs($_) > $pk }
ok(abs($pk - 0.177) < 0.01, "-6 dB fader + centre pan: peak $pk");
my $lv = $m->levels;
ok($lv->{master}[0]{peak} > 0.15 && $lv->{master}[0]{peak} < 0.2, 'master meter');
ok($lv->{strips}[0]{peak} > 0.2, 'strip meter');
$m->set_mute(0, 1); ($L, $R) = $m->process([\@tone], 256) for 1 .. 5;
ok(abs($L->[100]) < 1e-6, 'mute silences');
