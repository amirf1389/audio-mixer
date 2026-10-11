// Objective-C API of the Audio Mixer engine (a thin layer over the embedded C core).
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Up to 32 mono strips (gain, pan, mute, solo) into a stereo master (gain, limiter) with meters, loudness and phase.
@interface AMEMixer : NSObject
- (instancetype)initWithStrips:(NSInteger)strips sampleRate:(double)sampleRate NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;
@property (nonatomic, readonly) NSInteger strips;
- (void)setGainDb:(double)db forStrip:(NSInteger)strip;       // -60 and below = silence, +10 at most
- (void)setPan:(double)pan forStrip:(NSInteger)strip;         // -1 left .. +1 right, constant power
- (void)setMute:(BOOL)on forStrip:(NSInteger)strip;
- (void)setSolo:(BOOL)on forStrip:(NSInteger)strip;           // with any solo on, only the solo strips are heard
- (void)setMasterDb:(double)db;
- (void)setLimiterOn:(BOOL)on ceilingDb:(double)ceilingDb releaseMs:(double)releaseMs;
- (void)setClipThresholdDb:(double)db;
/// inputs: one pointer per strip (nil = silent) to `frames` floats; outLeft / outRight: `frames` floats each.
- (void)processInputs:(const float *_Nullable *_Nonnull)inputs frames:(NSUInteger)frames outLeft:(float *)outLeft outRight:(float *)outRight;
- (double)peakOfStrip:(NSInteger)strip;
@property (nonatomic, readonly) double masterPeakLeft;
@property (nonatomic, readonly) double masterPeakRight;
@property (nonatomic, readonly) double momentaryLoudness;     // LUFS
@property (nonatomic, readonly) double integratedLoudness;    // LUFS, gated
@property (nonatomic, readonly) double truePeak;              // dBTP
@property (nonatomic, readonly) double correlation;           // phase, -1 .. 1
@end

/// BS.1770-4 loudness meter.
@interface AMELoudness : NSObject
- (instancetype)initWithSampleRate:(double)sampleRate NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;
- (void)feedLeft:(const float *)left right:(const float *)right frames:(NSUInteger)frames;
- (void)reset;
@property (nonatomic, readonly) double momentary;
@property (nonatomic, readonly) double shortTerm;
@property (nonatomic, readonly) double integrated;
@property (nonatomic, readonly) double range;
@property (nonatomic, readonly) double truePeak;
@end

NS_ASSUME_NONNULL_END
