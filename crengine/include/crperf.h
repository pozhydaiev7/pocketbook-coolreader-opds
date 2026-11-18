/** \file crperf.h
    \brief Counters of the time the engine spends on parts of its work, for the log of a frontend.

    A frontend that writes timing lines to its log turns the counters on, resets them before a
    piece of work (an event, a page) and reads them after it. Turned off, a counted scope costs
    one test of a flag.
*/
#ifndef CRPERF_H_INCLUDED
#define CRPERF_H_INCLUDED

struct CRPerfCounters {
    bool on;            ///< counted only while a frontend asks for it
    int formats;        ///< final blocks (paragraphs) formatted: LFormattedText::Format
    double formatMs;
    int formatDepth;    ///< a block formatted inside another one is counted, its time is not added twice
    int images;         ///< pictures drawn to a gray buffer, with their decoding
    double imageMs;
    int imageDepth;
    int rescales;       ///< pictures scaled from a buffer: LVGrayDrawBuf::DrawRescaled
    double rescaleMs;
    int rescaleDepth;
    void reset()
    {
        formats = images = rescales = 0;
        formatMs = imageMs = rescaleMs = 0;
        formatDepth = imageDepth = rescaleDepth = 0;
    }
};

extern CRPerfCounters crPerf;

/// milliseconds of a clock that only goes on, with fractions
double crPerfNowMs();

/// Counts one piece of work and the time of its scope
struct CRPerfScope {
    int & _count;
    double & _ms;
    int & _depth;
    double _t0;
    bool _on;
    CRPerfScope( int & count, double & ms, int & depth )
        : _count(count), _ms(ms), _depth(depth), _t0(0), _on(crPerf.on)
    {
        if ( _on && _depth++ == 0 )
            _t0 = crPerfNowMs();
    }
    ~CRPerfScope()
    {
        if ( !_on )
            return;
        _count++;
        if ( --_depth == 0 )
            _ms += crPerfNowMs() - _t0;
    }
};

#endif // CRPERF_H_INCLUDED
