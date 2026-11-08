#ifndef OS999_QUICKTIME_MOVIE_H
#define OS999_QUICKTIME_MOVIE_H
#include <stddef.h>
#include <stdint.h>
/* Native OS999 host API inspired by Movie Toolbox operations. These signatures
 * are NOT Apple's ABI, a classic guest trap implementation, or QuickTime itself.
 * Times use milliseconds. Compressed decoding uses separately linked FFmpeg. */
typedef struct QuickTimeMovie QuickTimeMovie;
int NewMovieFromFile(QuickTimeMovie **out, const char *path, char *error, size_t n);
void DisposeMovie(QuickTimeMovie *movie);
void StartMovie(QuickTimeMovie *movie);
void StopMovie(QuickTimeMovie *movie);
uint32_t GetMovieTime(const QuickTimeMovie *movie);
uint32_t GetMovieDuration(const QuickTimeMovie *movie);
int SetMovieTime(QuickTimeMovie *movie, uint32_t milliseconds);
int MoviesTask(QuickTimeMovie *movie, uint32_t host_ms);
int GetMovieState(const QuickTimeMovie *movie);
const uint32_t *GetMoviePixels(const QuickTimeMovie *movie, int *w, int *h);
void SetMovieVolume(QuickTimeMovie *movie, float volume);
float GetMovieVolume(const QuickTimeMovie *movie);
/* Actual decoder path, not proof of end-to-end GPU composition. */
int MovieHardwareDecoded(const QuickTimeMovie *movie);
const char *GetMovieCodec(const QuickTimeMovie *movie);
void quicktime_install_backend(void);
#endif
