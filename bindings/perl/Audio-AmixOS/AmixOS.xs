#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "amix.h"

/* a Perl array of numbers -> a float buffer (missing / short arrays read as silence) */
static void av_to_floats(pTHX_ AV* av, float* out, int n) { int i; for (i = 0; i < n; ++i) { SV** e = av ? av_fetch(av, i, 0) : NULL; out[i] = e ? (float)SvNV(*e) : 0.f; } }
static HV* meter_hv(pTHX_ const amix_meter* m) { HV* h = newHV(); hv_stores(h, "peak", newSVnv(m->peak)); hv_stores(h, "rms", newSVnv(m->rms)); hv_stores(h, "hold", newSVnv(m->hold)); hv_stores(h, "clips", newSVuv(m->clips)); hv_stores(h, "in_peak", newSVnv(m->in_peak)); return h; }

MODULE = Audio::AmixOS		PACKAGE = Audio::AmixOS

PROTOTYPES: DISABLE

const char*
version()
  CODE:
    RETVAL = amix_version();
  OUTPUT:
    RETVAL

int
api_version()
  CODE:
    RETVAL = amix_api_version();
  OUTPUT:
    RETVAL

SV*
devices_json()
  CODE:
    { char* s = amix_devices_json(); RETVAL = newSVpv(s ? s : "[]", 0); amix_free(s); }
  OUTPUT:
    RETVAL

SV*
backends_json()
  CODE:
    { char* s = amix_backends_json(); RETVAL = newSVpv(s ? s : "[]", 0); amix_free(s); }
  OUTPUT:
    RETVAL

SV*
license_json()
  CODE:
    { char* s = amix_license_json(); RETVAL = newSVpv(s ? s : "{}", 0); amix_free(s); }
  OUTPUT:
    RETVAL

SV*
machine_id()
  CODE:
    { char* s = amix_machine_id(); RETVAL = newSVpv(s ? s : "", 0); amix_free(s); }
  OUTPUT:
    RETVAL

MODULE = Audio::AmixOS		PACKAGE = Audio::AmixOS::Mixer		PREFIX = amix_mixer_

amix_mixer*
amix_mixer_new(klass, strips, sample_rate)
    const char* klass
    int strips
    double sample_rate
  CODE:
    RETVAL = amix_mixer_create(strips, sample_rate);
    if (!RETVAL) croak("amix_mixer_create failed");
  OUTPUT:
    RETVAL

void
amix_mixer_DESTROY(m)
    amix_mixer* m
  CODE:
    amix_mixer_destroy(m);

int
amix_mixer_strips(m)
    amix_mixer* m

void
amix_mixer_set_gain(m, strip, db)
    amix_mixer* m
    int strip
    double db

void
amix_mixer_set_pan(m, strip, pan)
    amix_mixer* m
    int strip
    double pan

void
amix_mixer_set_mute(m, strip, on)
    amix_mixer* m
    int strip
    int on

void
amix_mixer_set_solo(m, strip, on)
    amix_mixer* m
    int strip
    int on

void
amix_mixer_set_master(m, db)
    amix_mixer* m
    double db

void
amix_mixer_set_limiter(m, on, ceiling_db, release_ms)
    amix_mixer* m
    int on
    double ceiling_db
    double release_ms

void
amix_mixer_set_send(m, strip, bus, db, pre)
    amix_mixer* m
    int strip
    int bus
    double db
    int pre

void
amix_mixer_set_bus_level(m, bus, db)
    amix_mixer* m
    int bus
    double db

void
amix_mixer_set_bus_return(m, bus, db)
    amix_mixer* m
    int bus
    double db

void
amix_mixer_set_width(m, width)
    amix_mixer* m
    double width

void
amix_mixer_process(m, inputs, frames)
    amix_mixer* m
    SV* inputs
    int frames
  PREINIT:
    AV* ins; int n, s, i; float** bufs; const float** cin; float* oL; float* oR; AV* aL; AV* aR;
  PPCODE:
    if (frames <= 0 || frames > 65536) croak("frames must be 1..65536");
    if (!SvROK(inputs) || SvTYPE(SvRV(inputs)) != SVt_PVAV) croak("inputs must be an array ref of array refs (one per strip)");
    ins = (AV*)SvRV(inputs); n = amix_mixer_strips(m);
    Newxz(bufs, n, float*); Newxz(cin, n, const float*); Newxz(oL, frames, float); Newxz(oR, frames, float);
    for (s = 0; s < n; ++s) { SV** e = av_fetch(ins, s, 0); Newxz(bufs[s], frames, float); av_to_floats(aTHX_ (e && SvROK(*e) && SvTYPE(SvRV(*e)) == SVt_PVAV) ? (AV*)SvRV(*e) : NULL, bufs[s], frames); cin[s] = bufs[s]; }
    amix_mixer_process(m, cin, frames, oL, oR);
    aL = newAV(); aR = newAV(); av_extend(aL, frames - 1); av_extend(aR, frames - 1);
    for (i = 0; i < frames; ++i) { av_store(aL, i, newSVnv(oL[i])); av_store(aR, i, newSVnv(oR[i])); }
    for (s = 0; s < n; ++s) Safefree(bufs[s]);
    Safefree(bufs); Safefree(cin); Safefree(oL); Safefree(oR);
    EXTEND(SP, 2); PUSHs(sv_2mortal(newRV_noinc((SV*)aL))); PUSHs(sv_2mortal(newRV_noinc((SV*)aR)));

SV*
amix_mixer_levels(m)
    amix_mixer* m
  PREINIT:
    amix_levels lv; HV* h; AV* strips; AV* master; int i, n;
  CODE:
    amix_mixer_levels(m, &lv); h = newHV(); strips = newAV(); master = newAV(); n = amix_mixer_strips(m);
    for (i = 0; i < n; ++i) av_push(strips, newRV_noinc((SV*)meter_hv(aTHX_ &lv.strips[i])));
    for (i = 0; i < 2; ++i) av_push(master, newRV_noinc((SV*)meter_hv(aTHX_ &lv.master[i])));
    hv_stores(h, "strips", newRV_noinc((SV*)strips)); hv_stores(h, "master", newRV_noinc((SV*)master));
    hv_stores(h, "momentary_lufs", newSVnv(lv.momentary_lufs)); hv_stores(h, "short_term_lufs", newSVnv(lv.short_term_lufs)); hv_stores(h, "integrated_lufs", newSVnv(lv.integrated_lufs)); hv_stores(h, "true_peak_dbtp", newSVnv(lv.true_peak_dbtp)); hv_stores(h, "correlation", newSVnv(lv.correlation));
    RETVAL = newRV_noinc((SV*)h);
  OUTPUT:
    RETVAL
