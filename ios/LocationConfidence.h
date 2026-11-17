#import <LocationConfidenceSpec/LocationConfidenceSpec.h>

/// What JavaScript sees on iOS. React Native creates one of these and calls
/// its methods when JavaScript calls NativeLocationConfidence.something().
/// The list of methods comes from src/NativeLocationConfidence.ts.
@interface LocationConfidence : NSObject <NativeLocationConfidenceSpec>

@end
