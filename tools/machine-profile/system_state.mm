#import <Foundation/Foundation.h>

#include <cstdio>

int main() {
    @autoreleasepool {
        NSProcessInfo* process = NSProcessInfo.processInfo;
        NSString* thermal = nil;
        switch (process.thermalState) {
        case NSProcessInfoThermalStateNominal: thermal = @"nominal"; break;
        case NSProcessInfoThermalStateFair: thermal = @"fair"; break;
        case NSProcessInfoThermalStateSerious: thermal = @"serious"; break;
        case NSProcessInfoThermalStateCritical: thermal = @"critical"; break;
        }
        // Unknown future enum values remain unavailable rather than implying nominal.
        NSDictionary* state = @{
            @"thermal_state": thermal ?: NSNull.null,
            @"thermal_raw": @(process.thermalState),
            @"low_power_mode": @(process.lowPowerModeEnabled),
            @"physical_memory_bytes": @(process.physicalMemory),
            @"logical_cpu_count": @(process.processorCount),
            @"active_cpu_count": @(process.activeProcessorCount)
        };
        NSError* error = nil;
        NSData* data = [NSJSONSerialization dataWithJSONObject:state options:0 error:&error];
        if (data == nil || error != nil) {
            std::fputs("Unable to serialize system state\n", stderr);
            return 1;
        }
        if (std::fwrite(data.bytes, 1, data.length, stdout) != data.length ||
            std::fputc('\n', stdout) == EOF || std::fflush(stdout) != 0) {
            return 1;
        }
    }
}
