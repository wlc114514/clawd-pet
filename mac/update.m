/*
 * Looking for a newer release on GitHub. Not the API (60 asks an hour per
 * IP): releases/latest just redirects to the newest release's page, whose
 * last part is its tag. An app bundle can't swap itself out as simply as
 * the Windows exe does, so a new version is fetched from that page.
 */
#import "mac.h"
#include "version.h"

#define RELEASES @"https://github.com/wlc114514/clawd-pet/releases/"

void update_check(void (^done)(int result, NSString *tag))
{
    NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:RELEASES @"latest"]];
    rq.HTTPMethod = @"HEAD";
    rq.timeoutInterval = 15;
    [rq setValue:@"clawd-pet/" CLAWD_VERSION forHTTPHeaderField:@"User-Agent"];
    NSURLSessionDataTask *t = [NSURLSession.sharedSession dataTaskWithRequest:rq
                                                            completionHandler:^(NSData *data, NSURLResponse *r, NSError *err) {
        (void)data;
        int result = UPD_FAILED;
        NSString *tag = nil;
        // .../releases/tag/v1.2.3 (just .../releases while there are none)
        NSString *at = r.URL.absoluteString, *prefix = RELEASES @"tag/";
        if (!err && [(NSHTTPURLResponse *)r statusCode] == 200 && [at hasPrefix:prefix]) {
            tag = [at substringFromIndex:prefix.length];
            if (tag.length && tag.length < 32 && ![tag containsString:@"/"]) {
                result = version_newer(tag.UTF8String, CLAWD_VERSION) ? UPD_NEWER : UPD_LATEST;
            }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            done(result, result == UPD_NEWER ? tag : nil);
        });
    }];
    [t resume];
}

NSURL *update_page(NSString *tag)
{
    return [NSURL URLWithString:[RELEASES @"tag/" stringByAppendingString:tag]];
}
