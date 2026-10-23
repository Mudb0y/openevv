"""Run the actual native resampler in isolation with a host C compiler.

Usage: python check-resampling.py --cc /path/to/clang.exe
Checks four interpolation methods, large buffers, PCM bounds, DC gain and block continuity.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--cc', default='cc')
args = parser.parse_args()
source = (root / 'src/eci/sound/eci_pcm.c').read_text(encoding='utf-8')
# This is the native implementation, not a Python reimplementation of its math.
start = source.index('#include <stdlib.h>')
end = source.index('typedef struct AudioConverter')
harness = r'''
#include <assert.h>
#include <stdio.h>
int main(void) {
    const int n=50000, sizes[]={1,37,200,111,400,251};
    int32_t *input=malloc(n*sizeof(int32_t));
    int32_t *whole=malloc(n*7*sizeof(int32_t));
    int32_t *pieces=malloc(n*7*sizeof(int32_t));
    int methods[]={CVT_HOLD,CVT_LINEAR,CVT_CUBIC,CVT_SINC};
    int rates[]={16000,22050,24000,32000,44100,48000};
    PcmResampler r;
    assert(input && whole && pieces);
    assert(!pcm_resample_start(&r,0,48000,CVT_LINEAR));
    assert(!pcm_resample_start(&r,11025,0,CVT_SINC));
    assert(!pcm_resample_start(&r,11025,48000,99));
    for(int m=0;m<4;m++) for(int t=0;t<6;t++) {
        for(int i=0;i<n;i++) input[i]=(i<n/2)?1000:((i%2)?32767:-32768);
        assert(pcm_resample_start(&r,11025,rates[t],methods[m]));
        unsigned expected=pcm_resample_count(&r,n);
        unsigned count=pcm_resample(&r,input,n,whole);
        assert(count==expected);
        assert(count==(unsigned)(((int64_t)n*rates[t]+11024)/11025));
        unsigned settled=(unsigned)((int64_t)(2*pcm_resample_delay(&r)+2)*rates[t]/11025);
        for(unsigned i=settled;i<(unsigned)((int64_t)(n/2)*rates[t]/11025);i++) assert(whole[i]==1000);
        for(unsigned i=0;i<count;i++) assert(whole[i]>=-32768 && whole[i]<=32767);
        pcm_resample_end(&r);
        assert(pcm_resample_start(&r,11025,rates[t],methods[m]));
        unsigned at=0,made=0,k=0;
        while(at<(unsigned)n) {
            unsigned take=sizes[k++%6]; if(take>(unsigned)n-at) take=n-at;
            unsigned want=pcm_resample_count(&r,take);
            unsigned got=pcm_resample(&r,input+at,take,pieces+made);
            assert(want==got); made+=got; at+=take;
        }
        assert(made==count && memcmp(whole,pieces,count*sizeof(int32_t))==0);
        assert(pcm_resample(&r,input,0,pieces)==0);
        pcm_resample_end(&r);
    }
    /* Wideband synthesis now lowers rates too. Preserve that upstream path. */
    assert(pcm_resample_start(&r,22050,16000,CVT_SINC));
    unsigned count=pcm_resample(&r,input,n,whole);
    assert(count==(unsigned)(((int64_t)n*16000+22049)/22050));
    pcm_resample_end(&r);
    assert(pcm_resample_start(&r,22050,16000,CVT_SINC));
    unsigned made=0;
    for(int at=0;at<n;) {
        int take=n-at<37?n-at:37;
        made+=pcm_resample(&r,input+at,take,pieces+made); at+=take;
    }
    assert(made==count && memcmp(whole,pieces,count*sizeof(int32_t))==0);
    pcm_resample_end(&r);
    free(input); free(whole); free(pieces);
    puts("PASS: 4 methods x 6 rates; 50,000-sample blocks, continuity, DC, counts, PCM bounds and wideband downsampling.");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='openevv-resampling-') as directory:
    path = Path(directory)
    (path / 'resampler.c').write_text(source[start:end] + harness, encoding='utf-8')
    binary = path / 'check.exe'
    klatt = root / 'src/klatt'
    subprocess.run([args.cc, '-O2', '-fno-strict-aliasing', '-std=c99',
                    '-I', str(root / 'src/eci/sound'), '-I', str(klatt),
                    str(path / 'resampler.c'), str(klatt / 'klatt_fx.c'),
                    str(klatt / 'klatt_tables.c'), '-o', str(binary), '-lm'], check=True)
    subprocess.run([str(binary)], check=True, timeout=60)
