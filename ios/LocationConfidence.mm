#import "LocationConfidence.h"

// The generated Swift header has a different import path depending on
// whether the pod is built as a framework or as a static library.
#if __has_include(<LocationConfidence/LocationConfidence-Swift.h>)
#import <LocationConfidence/LocationConfidence-Swift.h>
#else
#import "LocationConfidence-Swift.h"
#endif

// TurboModule inbound adapter. It only converts Codegen types to Foundation
// types and forwards to Swift. No geographic logic lives here.
@implementation LocationConfidence

- (void)getAbiVersion:(RCTPromiseResolveBlock)resolve reject:(RCTPromiseRejectBlock)reject
{
  resolve(@([LCECoreBridge abiVersion]));
}

- (void)spikeEcho:(NSString *)requestId
            input:(JS::NativeLocationConfidence::SpikeInput &)input
          resolve:(RCTPromiseResolveBlock)resolve
           reject:(RCTPromiseRejectBlock)reject
{
  NSMutableArray<NSString *> *codes = [NSMutableArray new];
  for (NSString *code : input.codes()) {
    [codes addObject:code];
  }

  NSDictionary *result = [LCECoreBridge spikeEchoWithRequestId:requestId
                                                     sessionId:input.sessionId()
                                                  radiusMeters:input.radiusMeters()
                                                       enabled:input.enabled()
                                                          mode:input.mode()
                                                         codes:codes];
  resolve(result);
}

- (void)cancelRequest:(NSString *)requestId
{
  // Spike: nothing to cancel yet.
}

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
