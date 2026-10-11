#import "AMEngine.h"

#include "am_core.h"
#include <stdlib.h>

@implementation AMEMixer {
  amc_mixer_t *_m;
}
- (instancetype)initWithStrips:(NSInteger)strips sampleRate:(double)sampleRate {
  if ((self = [super init])) {
    _m = (amc_mixer_t *)malloc(sizeof(amc_mixer_t));
    amc_mixer_init(_m, (int)strips, sampleRate);
  }
  return self;
}
- (void)dealloc { free(_m); }
- (NSInteger)strips { return _m->n_strips; }
- (void)setGainDb:(double)db forStrip:(NSInteger)strip { amc_mixer_set_gain_db(_m, (int)strip, db); }
- (void)setPan:(double)pan forStrip:(NSInteger)strip { amc_mixer_set_pan(_m, (int)strip, pan); }
- (void)setMute:(BOOL)on forStrip:(NSInteger)strip { amc_mixer_set_mute(_m, (int)strip, on ? 1 : 0); }
- (void)setSolo:(BOOL)on forStrip:(NSInteger)strip { amc_mixer_set_solo(_m, (int)strip, on ? 1 : 0); }
- (void)setMasterDb:(double)db { amc_mixer_set_master_db(_m, db); }
- (void)setLimiterOn:(BOOL)on ceilingDb:(double)ceilingDb releaseMs:(double)releaseMs { amc_mixer_set_limiter(_m, on ? 1 : 0, ceilingDb, releaseMs); }
- (void)setClipThresholdDb:(double)db { amc_mixer_set_clip_db(_m, db); }
- (void)processInputs:(const float *_Nullable *_Nonnull)inputs frames:(NSUInteger)frames outLeft:(float *)outLeft outRight:(float *)outRight {
  const float *silence = calloc(frames ? frames : 1, sizeof(float));
  const float *ptrs[AMC_MAX_STRIPS];
  for (int i = 0; i < _m->n_strips; i++) ptrs[i] = inputs[i] ? inputs[i] : silence;
  amc_mixer_process(_m, ptrs, frames, outLeft, outRight);
  free((void *)silence);
}
- (double)peakOfStrip:(NSInteger)strip { return amc_mixer_strip_peak(_m, (int)strip); }
- (double)masterPeakLeft { return amc_mixer_master_peak(_m, 0); }
- (double)masterPeakRight { return amc_mixer_master_peak(_m, 1); }
- (double)momentaryLoudness { return _m->loudness.momentary; }
- (double)integratedLoudness { return _m->loudness.integrated; }
- (double)truePeak { return _m->loudness.true_peak; }
- (double)correlation { return _m->phase.corr; }
@end

@implementation AMELoudness {
  amc_loudness_t *_l;
}
- (instancetype)initWithSampleRate:(double)sampleRate {
  if ((self = [super init])) {
    _l = (amc_loudness_t *)malloc(sizeof(amc_loudness_t));
    amc_loudness_init(_l, sampleRate);
  }
  return self;
}
- (void)dealloc { free(_l); }
- (void)feedLeft:(const float *)left right:(const float *)right frames:(NSUInteger)frames { amc_loudness_feed(_l, left, right, frames); }
- (void)reset { amc_loudness_reset(_l); }
- (double)momentary { return _l->momentary; }
- (double)shortTerm { return _l->short_term; }
- (double)integrated { return _l->integrated; }
- (double)range { return _l->lra; }
- (double)truePeak { return _l->true_peak; }
@end
