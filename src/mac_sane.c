#include "mac_runtime.h"
#include "mac_sane_arith.h"
#include <string.h>

/* Independent bounded FP68K/Pack4 conversions. Public Apple Numerics Manual,
 * second edition, Appendix E (operand stack, formats and selector tables).
 * https://manuals.plus/m/1f06cc272b1e59ae58f6379edca5b7829815540f233f0679c0f5f26cb9dbcbde
 * All calculation is integer bit manipulation, never host extended/double.
 * NaNs, invalid integer conversions, general arithmetic and exception callbacks
 * fail before mutation. No full SANE/IEEE arithmetic claim is made. */
static int fail(MacRuntime *rt,const char *why){mac_cpu_halt(rt->cpu,why);return -1;}
static int range(MacRuntime *rt,uint32_t p,unsigned n) {
    size_t size=mac_cpu_ram_size(rt->cpu);
    return !(p&1)&&p<=size&&n<=size-p;
}
static uint16_t r16(const uint8_t *p){return (uint16_t)(p[0]*256u+p[1]);}
static uint32_t r32(const uint8_t *p){return (uint32_t)r16(p)<<16|r16(p+2);}
static void w16(uint8_t *p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void w32(uint8_t *p,uint32_t v){w16(p,(uint16_t)(v>>16));w16(p+2,(uint16_t)v);}
static Extended load(const uint8_t *p){return (Extended){r16(p),(uint64_t)r32(p+2)<<32|r32(p+6)};}
static void store(uint8_t *p,Extended x){w16(p,x.sign_exp);w32(p+2,(uint32_t)(x.significand>>32));w32(p+6,(uint32_t)x.significand);}
static Extended unsigned_extended(uint64_t magnitude,int negative) {
    Extended x={(uint16_t)(negative?0x8000:0),0};
    if(!magnitude)return x;
    unsigned highest=0;for(uint64_t v=magnitude;v>>=1;)highest++;
    x.sign_exp|=(uint16_t)(16383+highest);x.significand=magnitude<<(63-highest);return x;
}
static int single_extended(uint32_t bits,Extended *out) {
    uint32_t fraction=bits&0x7fffff,exponent=(bits>>23)&255;
    out->sign_exp=(uint16_t)((bits>>16)&0x8000);out->significand=0;
    if(exponent==255){if(fraction)return -1;out->sign_exp|=0x7fff;out->significand=UINT64_C(1)<<63;return 0;}
    if(exponent){out->sign_exp|=(uint16_t)(exponent+16383-127);out->significand=(uint64_t)(fraction|0x800000)<<40;}
    else if(fraction) {
        unsigned highest=0;for(uint32_t v=fraction;v>>=1;)highest++;
        out->sign_exp|=(uint16_t)(16383-149+(int)highest);
        out->significand=(uint64_t)fraction<<(63-highest);
    }
    return 0;
}
static int truncate(Extended *x) {
    int exponent=(x->sign_exp&0x7fff)-16383;
    if(!x->significand)return 0;
    if(exponent<0){x->significand=0;x->sign_exp&=0x8000;return 1;}
    if(exponent>=63)return 0;
    uint64_t mask=(UINT64_C(1)<<(63-exponent))-1,discarded=x->significand&mask;
    x->significand&=~mask;return discarded!=0;
}
static int integer_result(Extended x,unsigned bits,unsigned rounding,uint32_t *out,int *inexact) {
    int exponent=(x.sign_exp&0x7fff)-16383,negative=(x.sign_exp&0x8000)!=0;
    uint64_t whole=0;int fraction=0,half_compare=-1;
    if(x.significand) {
        if(exponent>=32)return -1;
        if(exponent<0) {
            fraction=1;
            if(exponent==-1)half_compare=x.significand==(UINT64_C(1)<<63)?0:
                x.significand>(UINT64_C(1)<<63)?1:-1;
        } else {
            unsigned shift=63-(unsigned)exponent;
            whole=x.significand>>shift;
            uint64_t remainder=x.significand&((UINT64_C(1)<<shift)-1),half=UINT64_C(1)<<(shift-1);
            fraction=remainder!=0;half_compare=remainder==half?0:remainder>half?1:-1;
        }
    }
    if(fraction && ((rounding==0&&(half_compare>0||(half_compare==0&&(whole&1)))) ||
                    (rounding==1&&!negative) || (rounding==2&&negative)))whole++;
    uint64_t limit=UINT64_C(1)<<(bits-1);
    if(whole>(negative?limit:limit-1))return -1;
    *out=negative?(uint32_t)(0-(uint32_t)whole):(uint32_t)whole;*inexact=fraction;return 0;
}
int mac_sane_service(MacRuntime *rt,uint16_t opcode) {
    if(opcode!=0xa9eb)return 0;
    if(!rt||!rt->cpu||mac_cpu_halted(rt->cpu))return -1;
    uint32_t sp=mac_cpu_get(rt->cpu,MAC_A7);
    if(!range(rt,sp,2))return fail(rt,"Invalid SANE selector stack.");
    uint8_t *ram=mac_cpu_ram(rt->cpu);unsigned op=r16(ram+sp);
    unsigned args=10,src_size=10,dst_size=10;
    switch(op) {
    case 0x100e:src_size=4;break;
    case 0x200e:src_size=2;break;
    case 0x280e:src_size=4;break;
    case 0x000e:case 0x0010:break;
    case 0x0000:case 0x0002:case 0x0004:case 0x0006:break;   /* FADDX, FSUBX, FMULX, FDIVX: dst := dst op src, both extended */
    case 0x2010:dst_size=2;break;
    case 0x1010:dst_size=4;break;   /* FX2S: extended to single */
    case 0x0810:dst_size=8;break;   /* FX2D: extended to double */
    case 0x080e:src_size=8;break;   /* FD2X: double to extended */
    case 0x2810:dst_size=4;break;
    case 0x0016:args=6;src_size=0;break;
    case 0x0001:case 0x0003:args=6;src_size=0;dst_size=2;break;
    default:return fail(rt,"Unsupported SANE FP68K selector.");
    }
    if(!range(rt,sp,args))return fail(rt,"Invalid SANE operand stack.");
    uint32_t dst=r32(ram+sp+2),src=src_size?r32(ram+sp+6):dst;
    if(!dst||(src_size&&!src)||!range(rt,dst,dst_size)||(src_size&&!range(rt,src,src_size)))
        return fail(rt,"Invalid SANE operand address.");
    uint8_t output[10]={0};uint16_t environment=rt->sane_environment;int inexact=0;
    if(op==1)environment=r16(ram+dst);
    else if(op==3)w16(output,environment);
    else {
        if(environment&0x0060)return fail(rt,"SANE nonextended rounding precision is unsupported.");
        Extended x={0};
        if(op==0x100e){if(single_extended(r32(ram+src),&x))return fail(rt,"SANE single NaN conversion is unsupported.");}
        else if(op==0x080e) {
            const char *why="SANE double conversion failed.";
            if(mac_sane_from_double((uint64_t)r32(ram+src)<<32|r32(ram+src+4),&x,&why))return fail(rt,why);
        } else if(op==0x200e||op==0x280e) {
            int64_t value=op==0x200e?(int16_t)r16(ram+src):(int32_t)r32(ram+src);
            x=unsigned_extended((uint64_t)(value<0?-value:value),value<0);
        } else x=load(ram+src);
        unsigned exponent=x.sign_exp&0x7fff;
        if((exponent && exponent!=0x7fff && !(x.significand>>63)) ||
           (!exponent && (x.significand>>63)))
            return fail(rt,"SANE unnormalized extended operand is unsupported.");
        if(exponent==0x7fff && x.significand!=(UINT64_C(1)<<63))return fail(rt,"SANE extended NaN conversion is unsupported.");
        if(op==0x0000||op==0x0002||op==0x0004||op==0x0006) {
            const char *why="SANE arithmetic failed.";Extended y=load(ram+dst),z;
            if(mac_sane_arith(op,y,x,(environment>>13)&3,&z,&inexact,&why))return fail(rt,why);
            store(output,z);
        } else if(op==0x0016) {
            if(exponent!=0x7fff)inexact=truncate(&x);
            store(output,x);
        } else if(op==0x1010||op==0x0810) {
            const char *why="SANE float conversion failed.";uint64_t fbits;
            if(mac_sane_to_float(x,op==0x1010?23:52,op==0x1010?8:11,(environment>>13)&3,&fbits,&inexact,&why))return fail(rt,why);
            if(dst_size==4)w32(output,(uint32_t)fbits);else{w32(output,(uint32_t)(fbits>>32));w32(output+4,(uint32_t)fbits);}
        } else if(op==0x2010||op==0x2810) {
            uint32_t integer;
            if(exponent==0x7fff||integer_result(x,dst_size*8,(environment>>13)&3,&integer,&inexact))
                return fail(rt,"SANE invalid integer conversion is unsupported.");
            if(dst_size==2)w16(output,(uint16_t)integer);else w32(output,integer);
        } else store(output,x);
        if(inexact && (environment&0x10))return fail(rt,"SANE inexact exception callback is unsupported.");
        if(inexact)environment|=0x1000;
    }
    if(op!=1)memcpy(ram+dst,output,dst_size);
    rt->sane_environment=environment;mac_cpu_set(rt->cpu,MAC_A7,sp+args);return 1;
}
