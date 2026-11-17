#import "LocationConfidence.h"

#import <React/RCTInvalidating.h>

// The generated Swift header has a different import path depending on
// whether the pod is built as a framework or as a static library.
#if __has_include(<LocationConfidence/LocationConfidence-Swift.h>)
#import <LocationConfidence/LocationConfidence-Swift.h>
#else
#import "LocationConfidence-Swift.h"
#endif

/*
 * The first stop of a call from JavaScript on iOS.
 *
 * React Native generates C++ types for the arguments (the JS::... structs
 * below). Swift cannot read C++, so this file does one thing only: it turns
 * those types into plain dictionaries and numbers and hands them to Swift
 * (LocationConfidenceBridge.swift), where the real work starts.
 *
 * No rule about locations lives here.
 */

@interface LocationConfidence () <RCTInvalidating>
@end

@implementation LocationConfidence {
  LCEBridge *_bridge;
}

// The bridge reads the permissions and watches the app going to the
// background. iOS wants that set up on the main thread.
+ (BOOL)requiresMainQueueSetup
{
  return YES;
}

- (instancetype)init
{
  if (self = [super init]) {
    if ([NSThread isMainThread]) {
      _bridge = [LCEBridge new];
    } else {
      __block LCEBridge *bridge = nil;
      dispatch_sync(dispatch_get_main_queue(), ^{
        bridge = [LCEBridge new];
      });
      _bridge = bridge;
    }
  }
  return self;
}

// React Native is tearing the module down (reload, or the app is closing).
- (void)invalidate
{
  [_bridge invalidate];
}

/// Swift rejects with a code and a message; React Native wants a third
/// argument, an NSError, which is not used here.
static void (^rejecter(RCTPromiseRejectBlock reject))(NSString *, NSString *)
{
  return ^(NSString *code, NSString *message) {
    reject(code, message, nil);
  };
}

// --- Capabilities and permissions --------------------------------------------

- (void)getCapabilities:(RCTPromiseResolveBlock)resolve reject:(RCTPromiseRejectBlock)reject
{
  [_bridge getCapabilities:resolve];
}

- (void)getAuthorization:(RCTPromiseResolveBlock)resolve reject:(RCTPromiseRejectBlock)reject
{
  [_bridge getAuthorization:resolve];
}

- (void)requestLocationPermission:(RCTPromiseResolveBlock)resolve
                           reject:(RCTPromiseRejectBlock)reject
{
  [_bridge requestLocationPermission:resolve reject:rejecter(reject)];
}

- (void)requestMotionPermission:(RCTPromiseResolveBlock)resolve
                         reject:(RCTPromiseRejectBlock)reject
{
  [_bridge requestMotionPermission:resolve];
}

// --- Sessions -----------------------------------------------------------------

- (void)startSession:(JS::NativeLocationConfidence::NativeSessionOptions &)options
             resolve:(RCTPromiseResolveBlock)resolve
              reject:(RCTPromiseRejectBlock)reject
{
  [_bridge startSessionWithKeepCollectingReadings:options.keepCollectingReadings()
                            timeBetweenReadingsMs:options.timeBetweenReadingsMs()
                          longestCollectingTimeMs:options.longestCollectingTimeMs()
                                          resolve:resolve
                                           reject:rejecter(reject)];
}

- (void)clearHistory:(NSString *)sessionId
             resolve:(RCTPromiseResolveBlock)resolve
              reject:(RCTPromiseRejectBlock)reject
{
  [_bridge clearHistory:sessionId resolve:resolve reject:rejecter(reject)];
}

- (void)stopSession:(NSString *)sessionId
            resolve:(RCTPromiseResolveBlock)resolve
             reject:(RCTPromiseRejectBlock)reject
{
  [_bridge stopSession:sessionId resolve:resolve reject:rejecter(reject)];
}

// --- Evaluations ----------------------------------------------------------------

- (void)evaluate:(NSString *)requestId
         request:(JS::NativeLocationConfidence::NativeEvaluateRequest &)request
         resolve:(RCTPromiseResolveBlock)resolve
          reject:(RCTPromiseRejectBlock)reject
{
  NSString *sessionId = request.sessionId();

  NSDictionary *fields = @{
    @"sessionId" : sessionId ?: [NSNull null],
    @"latitude" : @(request.latitude()),
    @"longitude" : @(request.longitude()),
    @"radiusMeters" : @(request.radiusMeters()),
    @"totalDeadlineMs" : @(request.totalDeadlineMs()),
    @"initialFixBudgetMs" : @(request.initialFixBudgetMs()),
    @"recoveryWindowMs" : @(request.recoveryWindowMs()),
    @"maxFreshAgeMs" : @(request.maxFreshAgeMs()),
    @"maxFallbackAgeMs" : @(request.maxFallbackAgeMs()),
    @"maxHorizontalAccuracyMeters" : @(request.maxHorizontalAccuracyMeters()),
    @"enableMotionAssistance" : @(request.enableMotionAssistance()),
    @"allowHistoricalFallback" : @(request.allowHistoricalFallback()),
    @"maxPlausibleSpeedMps" : @(request.maxPlausibleSpeedMps()),
    @"zeroScoreForMockLocation" : @(request.zeroScoreForMockLocation()),
  };

  [_bridge evaluate:requestId request:fields resolve:resolve reject:rejecter(reject)];
}

- (void)cancelRequest:(NSString *)requestId
{
  [_bridge cancelRequest:requestId];
}

// --- Simulation -----------------------------------------------------------------

- (void)setSimulationEnabled:(BOOL)enabled
                     resolve:(RCTPromiseResolveBlock)resolve
                      reject:(RCTPromiseRejectBlock)reject
{
  [_bridge setSimulationEnabled:enabled resolve:resolve reject:rejecter(reject)];
}

- (void)simulateReadings:(NSArray *)readings
                 resolve:(RCTPromiseResolveBlock)resolve
                  reject:(RCTPromiseRejectBlock)reject
{
  // An array of objects already arrives as plain dictionaries.
  [_bridge simulateReadings:readings resolve:resolve reject:rejecter(reject)];
}

- (void)simulatePhoneActivity:(double)activity
                   confidence:(double)confidence
                      resolve:(RCTPromiseResolveBlock)resolve
                       reject:(RCTPromiseRejectBlock)reject
{
  [_bridge simulatePhoneActivity:activity
                      confidence:confidence
                         resolve:resolve
                          reject:rejecter(reject)];
}

// --- What React Native needs to find the module ----------------------------------

- (std::shared_ptr<facebook::react::TurboModule>)getTurboModule:
    (const facebook::react::ObjCTurboModule::InitParams &)params
{
  return std::make_shared<facebook::react::NativeLocationConfidenceSpecJSI>(params);
}

+ (NSString *)moduleName
{
  return @"LocationConfidence";
}

@end
