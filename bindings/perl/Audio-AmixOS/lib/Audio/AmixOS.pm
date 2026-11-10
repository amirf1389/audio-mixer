package Audio::AmixOS;
use strict; use warnings;
our $VERSION = '0.1.0';
require XSLoader; XSLoader::load('Audio::AmixOS', $VERSION);
1;
__END__

=head1 NAME

Audio::AmixOS - the A mix OS audio engine from Perl (offline mixer, meters, devices, licence)

=head1 SYNOPSIS

  use Audio::AmixOS;
  print Audio::AmixOS::version(), "\n";
  my $m = Audio::AmixOS::Mixer->new(4, 48000);
  $m->set_gain(0, -6); $m->set_pan(0, 0);
  my ($L, $R) = $m->process([ \@tone ], 256);     # one array ref per strip, 256 frames -> two array refs
  my $lv = $m->levels;                            # {strips => [{peak, rms, hold, clips, in_peak}, ...], master => [...], momentary_lufs, ...}
  print Audio::AmixOS::devices_json(), "\n";      # the sound cards this machine has, as JSON

=head1 DESCRIPTION

A thin XS layer over the C SDK (C<sdk/c/include/amix.h>, C<libamix>). The offline mixer runs the same code as the console: faders,
pans, mutes, solos, sends, buses, master limiter, width, meters and loudness. Opening a sound card (the live runtime) is in the C API;
from Perl use the daemon's HTTP API for that, or the console itself.

=head1 FUNCTIONS

version, api_version, devices_json, backends_json, license_json, machine_id.

=head1 Audio::AmixOS::Mixer

new(strips, sample_rate), strips, set_gain, set_pan, set_mute, set_solo, set_master, set_limiter(on, ceiling_db, release_ms),
set_send(strip, bus, db, pre), set_bus_level, set_bus_return, set_width, process(\@inputs, frames) -> (\@left, \@right), levels.

=head1 LICENSE

The binding follows the repository licence. The engine's Steinberg ASIO support is in the native library only (dual licence).

=cut
