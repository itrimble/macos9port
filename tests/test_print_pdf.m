#import <AppKit/AppKit.h>
#import <PDFKit/PDFKit.h>
#define Rect OspiRect
#include "finder_print.h"
#undef Rect
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); } } while(0)
void dialog_info(Session *s, const char *title, const char *body) {
    (void)s;fprintf(stderr,"Unexpected dialog: %s: %s\n",title,body);failures++;
}
static size_t pages(const char *path) {
    CFURLRef url=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8 *)path,strlen(path),false);
    CGPDFDocumentRef doc=CGPDFDocumentCreateWithURL(url);CFRelease(url);
    if(!doc)return 0;
    size_t n=CGPDFDocumentGetNumberOfPages(doc);CGPDFDocumentRelease(doc);return n;
}
int main(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        char root[]="/tmp/os999-print-XXXXXX",path[1024];CHECK(mkdtemp(root));
        Session *s=calloc(1,sizeof *s);CHECK(s);if(!s)return 1;
        s->print_request=PRINT_TEXT;strcpy(s->print_title,"Print pagination fixture");
        size_t length=0;
        for(int i=0;i<200;i++)length+=(size_t)snprintf(s->print_text+length,sizeof s->print_text-length,"Line %03d: Finder printing pagination and text wrapping.\n",i);
        snprintf(path,sizeof path,"%s/text.pdf",root);
        CHECK(!desktop_print_export_pdf(s,NULL,path));CHECK(pages(path)>=3);
        PDFDocument *document=[[PDFDocument alloc] initWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path]]];
        CHECK([[document string] containsString:@"Line 000"] && [[document string] containsString:@"Line 199"]);
        [document release];CHECK(!unlink(path));
        s->print_request=PRINT_WINDOW;s->print_rect=(OspiRect){10,20,400,300};
        uint32_t *frame=calloc(OSPI_W*OSPI_H,sizeof *frame);CHECK(frame);
        if(frame) {
            for(int y=0;y<OSPI_H;y++)for(int x=0;x<OSPI_W;x++)frame[y*OSPI_W+x]=0xff000000u|((uint32_t)(x%256)<<16)|((uint32_t)(y%256)<<8);
            snprintf(path,sizeof path,"%s/window.pdf",root);
            CHECK(!desktop_print_export_pdf(s,frame,path));CHECK(pages(path)==1);CHECK(!unlink(path));
            s->print_rect.x=-1;CHECK(desktop_print_export_pdf(s,frame,path)<0);CHECK(access(path,F_OK)<0);
            free(frame);
        }
        free(s);CHECK(!rmdir(root));
    }
    printf("Native print PDF: %d checks, %d failures\n",checks,failures);return !!failures;
}
