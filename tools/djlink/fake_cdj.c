/* Host tool: a fake CDJ for the DJ Link HIL tests (v338 neighbour panel).
 *
 * Broadcasts the keep-alive (port 50000) and a CDJ status carrying a loaded
 * track (port 50002) of a player that does not exist, plus, with --position,
 * the absolute position 0x0b (port 50001) like a CDJ-3000. The track is only
 * named: its source device, slot and rekordbox id. The P4 then asks that
 * device's dbserver (e.g. vynull, whose status is an idle template) for the
 * title, artist and waveform. Built with the same esp-djlink codec the P4
 * parses with, so the bytes are the ones it expects.
 *
 *   cd firmware/common/djlink
 *   gcc -O2 -I include -o /tmp/fake_cdj ../../../tools/djlink/fake_cdj.c \
 *       $(find src -name '*.c') -lm
 *   ./fake_cdj --ip 192.168.1.20 --source 17 --slot 4 --id 8 --bpm 128 --playing
 *
 * Nothing is sent to the P4 beyond DJ Link broadcasts; Ctrl-C stops it and the
 * player drops off the P4's table after 5 s. Use a player number the real
 * network does not use (default 5).
 */
#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <getopt.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "djlink/beat.h"
#include "djlink/mixer.h"
#include "djlink/packet.h"
#include "djlink/status.h"

static volatile sig_atomic_t s_stop;

static void on_sig(int s)
{
    (void)s;
    s_stop = 1;
}

static int64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + (int64_t)ts.tv_nsec / 1000;
}

static uint64_t now_ms(void)
{
    return (uint64_t)(now_us() / 1000);
}

typedef struct {
    const char *name;
    int player, source, slot;
    unsigned id, length_s;
    double bpm, pitch;
    unsigned long start_ms;
    int playing, position, no_track;
    int beats, master;          /* v339: beat packets 0x28, tempo master flag */
    int jitter_us;              /* v339: +/- on every beat packet's send time */
} cfg_t;

static double effective_bpm(const cfg_t *c)
{
    return c->bpm * (1.0 + c->pitch / 100.0);
}

static size_t build_status(const cfg_t *c, unsigned long ms, uint32_t counter, uint8_t *out)
{
    djlink_status_t s;
    memset(&s, 0, sizeof(s));
    djlink_name_from_str(c->name, s.name);
    s.device_number = (uint8_t)c->player;
    const int on = c->playing && !c->no_track;
    s.active = on;
    if (!c->no_track) {
        s.source_device = (uint8_t)c->source;
        s.source_slot = (uint8_t)c->slot;
        s.rekordbox_id = c->id;
    }
    s.play_state = c->no_track ? DJLINK_PLAY_NO_TRACK
                               : (c->playing ? DJLINK_PLAY_PLAYING : DJLINK_PLAY_PAUSE);
    s.flags = (uint8_t)(DJLINK_FLAG_BPM_MODE | DJLINK_FLAG_SYNC | DJLINK_FLAG_ON_AIR |
                        (on ? DJLINK_FLAG_PLAYING : 0) | (c->master ? DJLINK_FLAG_MASTER : 0));
    s.master_meaningful = c->master ? 1u : 0u;
    s.pitch_raw = (int32_t)(0x100000 * (1.0 + c->pitch / 100.0));
    s.bpm100 = c->no_track ? 0xffffu : (uint16_t)(c->bpm * 100.0 + 0.5);
    s.master_handoff = 0xff;
    s.beat = on ? (uint32_t)((double)ms * effective_bpm(c) / 60000.0) + 1u : 0xffffffffu;
    s.beat_in_bar = on ? (uint8_t)((s.beat - 1u) % 4u + 1u) : 0u;
    return (size_t)djlink_status_build(&s, counter, out, DJLINK_STATUS_PACKET_LEN);
}

static size_t build_position(const cfg_t *c, unsigned long ms, uint8_t *out)
{
    djlink_position_t p;
    memset(&p, 0, sizeof(p));
    djlink_name_from_str(c->name, p.name);
    p.device_number = (uint8_t)c->player;
    p.track_length_s = c->length_s;
    p.playhead_ms = (uint32_t)ms;
    p.pitch_x100 = (int32_t)(c->pitch * 100.0);
    p.bpm10 = (int32_t)(effective_bpm(c) * 10.0);
    return (size_t)djlink_position_build(&p, out, DJLINK_POSITION_PACKET_LEN);
}

static size_t build_beat(const cfg_t *c, uint32_t beat_index, uint8_t *out)
{
    const double period_ms = 60000.0 / effective_bpm(c);
    djlink_beat_t b;
    memset(&b, 0, sizeof(b));
    djlink_name_from_str(c->name, b.name);
    b.device_number = (uint8_t)c->player;
    b.next_beat_ms = (uint32_t)lround(period_ms);
    b.second_beat_ms = (uint32_t)lround(period_ms * 2.0);
    b.fourth_beat_ms = (uint32_t)lround(period_ms * 4.0);
    b.eighth_beat_ms = (uint32_t)lround(period_ms * 8.0);
    b.next_bar_ms = (uint32_t)lround(period_ms * (4 - (int)(beat_index % 4u)));
    b.second_bar_ms = b.next_bar_ms + (uint32_t)lround(period_ms * 4.0);
    b.pitch_raw = (int32_t)(0x100000 * (1.0 + c->pitch / 100.0));
    b.bpm100 = (uint16_t)(c->bpm * 100.0 + 0.5);
    b.beat_in_bar = (uint8_t)(beat_index % 4u + 1u);
    return (size_t)djlink_beat_build(&b, out, DJLINK_BEAT_PACKET_LEN);
}

static int open_sock(const char *ip)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    struct sockaddr_in a = { .sin_family = AF_INET };
    inet_pton(AF_INET, ip, &a.sin_addr);
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        perror("bind");
        exit(1);
    }
    return fd;
}

static void send_to(int fd, const char *dst, unsigned port, const uint8_t *buf, size_t len)
{
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port) };
    inet_pton(AF_INET, dst, &a.sin_addr);
    sendto(fd, buf, len, 0, (struct sockaddr *)&a, sizeof(a));
}

/* v341: a made-up DJM. Fader start / stop for player `target` (3 packets, 100 ms
 * apart, then the channel rests on IGNORE), and, with `on_air`, the channels on
 * air every 300 ms until Ctrl-C. */
static int run_mixer(const char *ip, const char *bcast, int target, const char *fader,
                     const char *on_air)
{
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    const int fd = open_sock(ip);
    uint8_t buf[DJLINK_FADER_PACKET_LEN];
    if (fader) {
        const djlink_fader_cmd_t cmd = !strcmp(fader, "start")  ? DJLINK_FADER_START
                                     : !strcmp(fader, "stop")   ? DJLINK_FADER_STOP
                                                                : DJLINK_FADER_IGNORE;
        for (int i = 0; i < 3 && !s_stop; i++) {
            djlink_fader_t f;
            memset(&f, 0, sizeof(f));
            djlink_name_from_str("DJM-TEST", f.name);
            f.player_number = 0x21;
            for (int k = 0; k < 4; k++) {
                f.players[k] = DJLINK_FADER_IGNORE;
            }
            if (target >= 1 && target <= 4) {
                f.players[target - 1] = cmd;
            }
            djlink_fader_build(&f, buf, sizeof(buf));
            send_to(fd, bcast, 50001, buf, DJLINK_FADER_PACKET_LEN);
            usleep(100000);
        }
        printf("mixer: fader %s sent for player %d\n", fader, target);
        /* The channel goes back to rest, as a real mixer's does. */
        djlink_fader_t f;
        memset(&f, 0, sizeof(f));
        djlink_name_from_str("DJM-TEST", f.name);
        f.player_number = 0x21;
        for (int k = 0; k < 4; k++) {
            f.players[k] = DJLINK_FADER_IGNORE;
        }
        djlink_fader_build(&f, buf, sizeof(buf));
        send_to(fd, bcast, 50001, buf, DJLINK_FADER_PACKET_LEN);
    }
    if (on_air) {
        djlink_onair_t a;
        memset(&a, 0, sizeof(a));
        djlink_name_from_str("DJM-TEST", a.name);
        a.mixer_number = 0x21;
        for (const char *p = on_air; *p; p++) {
            if (*p >= '1' && *p <= '4') {
                a.on_air[*p - '1'] = 1;
            }
        }
        printf("mixer: channels on air \"%s\" every 300 ms (Ctrl-C ends)\n", on_air);
        while (!s_stop) {
            djlink_onair_build(&a, buf, sizeof(buf));
            send_to(fd, bcast, 50001, buf, DJLINK_ONAIR_PACKET_LEN);
            usleep(300000);
        }
    }
    close(fd);
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
            "usage: fake_cdj --ip A.B.C.D [--bcast A.B.C.D] [--player 5] [--name CDJ-TEST]\n"
            "                [--source N] [--slot 2|3|4] [--id RBID] [--bpm 128] [--pitch 0]\n"
            "                [--playing] [--position] [--length SECONDS] [--start-ms MS]\n"
            "                [--beats] [--jitter-us N] [--master] [--no-track] [--dump FILE]\n"
            "       fake_cdj --ip A.B.C.D [--bcast A.B.C.D] --mixer [--target 1..4]\n"
            "                [--fader start|stop] [--on-air 1,3]\n"
            "  --mixer   a made-up DJM instead of a player (v341): --fader sends a fader start\n"
            "            or stop for player --target, --on-air repeats the channels on air\n"
            "  --beats   also send beat packets 0x28 (port 50001) on the beat, while playing\n"
            "  --jitter-us N  each beat packet leaves up to N us early or late (network jitter)\n"
            "  --master  the status carries the tempo master flag (the P4 follows it)\n"
            "  --player  1..6, a number the real network does not use (default 5)\n"
            "  --source  device the track came from (default: --player); vynull's number\n"
            "            when you want the P4 to ask vynull\n"
            "  --slot    2 SD, 3 USB, 4 rekordbox library (default 4)\n"
            "  --dump    write one status and one position packet to FILE and exit\n");
    exit(2);
}

int main(int argc, char **argv)
{
    cfg_t c = { .name = "CDJ-TEST", .player = 5, .slot = 4, .id = 1, .length_s = 300,
                .bpm = 128.0 };
    const char *ip = NULL, *bcast = "255.255.255.255", *dump = NULL;
    const char *fader = NULL, *on_air = NULL;
    int mixer = 0, target = 3;

    static const struct option opts[] = {
        { "ip", 1, 0, 'i' }, { "bcast", 1, 0, 'b' }, { "player", 1, 0, 'p' },
        { "name", 1, 0, 'n' }, { "source", 1, 0, 's' }, { "slot", 1, 0, 'S' },
        { "id", 1, 0, 'd' }, { "bpm", 1, 0, 'B' }, { "pitch", 1, 0, 'P' },
        { "playing", 0, 0, 'y' }, { "position", 0, 0, 'o' }, { "length", 1, 0, 'l' },
        { "start-ms", 1, 0, 't' }, { "no-track", 0, 0, 'x' }, { "dump", 1, 0, 'D' },
        { "mixer", 0, 0, 'M' }, { "target", 1, 0, 'T' }, { "fader", 1, 0, 'F' },
        { "on-air", 1, 0, 'A' },
        { "beats", 0, 0, 'e' }, { "jitter-us", 1, 0, 'j' }, { "master", 0, 0, 'm' },
        { 0, 0, 0, 0 } };
    int o;
    while ((o = getopt_long(argc, argv, "", opts, NULL)) != -1) {
        switch (o) {
        case 'i': ip = optarg; break;
        case 'b': bcast = optarg; break;
        case 'p': c.player = atoi(optarg); break;
        case 'n': c.name = optarg; break;
        case 's': c.source = atoi(optarg); break;
        case 'S': c.slot = atoi(optarg); break;
        case 'd': c.id = (unsigned)strtoul(optarg, NULL, 0); break;
        case 'B': c.bpm = atof(optarg); break;
        case 'P': c.pitch = atof(optarg); break;
        case 'y': c.playing = 1; break;
        case 'o': c.position = 1; break;
        case 'l': c.length_s = (unsigned)strtoul(optarg, NULL, 0); break;
        case 't': c.start_ms = strtoul(optarg, NULL, 0); break;
        case 'x': c.no_track = 1; break;
        case 'D': dump = optarg; break;
        case 'M': mixer = 1; break;
        case 'T': target = atoi(optarg); break;
        case 'F': fader = optarg; break;
        case 'A': on_air = optarg; break;
        case 'e': c.beats = 1; break;
        case 'j': c.jitter_us = atoi(optarg); break;
        case 'm': c.master = 1; break;
        default: usage();
        }
    }
    if ((!ip && !dump) || c.player < 1 || c.player > 6) {
        usage();
    }
    if (mixer) {
        if (!ip || (!fader && !on_air)) {
            usage();
        }
        return run_mixer(ip, bcast, target, fader, on_air);
    }
    if (c.source == 0) {
        c.source = c.player;
    }

    uint8_t st[DJLINK_STATUS_PACKET_LEN], ka[DJLINK_KEEPALIVE_PACKET_LEN],
        pos[DJLINK_POSITION_PACKET_LEN];

    if (dump) {
        build_status(&c, c.start_ms, 0, st);
        build_position(&c, c.start_ms, pos);
        FILE *f = fopen(dump, "wb");
        if (!f) {
            perror("dump");
            return 1;
        }
        fwrite(st, 1, sizeof(st), f);
        fwrite(pos, 1, sizeof(pos), f);
        fclose(f);
        printf("wrote %zu + %zu bytes to %s\n", sizeof(st), sizeof(pos), dump);
        return 0;
    }

    struct in_addr ina = { 0 };
    inet_pton(AF_INET, ip, &ina);
    djlink_keepalive_t k = { .name = c.name, .device_number = (uint8_t)c.player,
                             .mac = { 0x02, 0x00, 0x00, 0xde, 0xad, (uint8_t)c.player },
                             .ip = ntohl(ina.s_addr), .peer_count = 2, .startup_flags = 1 };
    djlink_keepalive_build(&k, ka, sizeof(ka));

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    const int fd = open_sock(ip);
    printf("fake CDJ #%d \"%s\" on %s -> %s: %s%s, id %u source %d slot %d, %.1f BPM%s\n",
           c.player, c.name, ip, bcast, c.no_track ? "no track" : "track",
           c.playing ? " (playing)" : "", c.id, c.source, c.slot, c.bpm,
           c.position ? ", with 0x0b position" : "");

    const uint64_t t0 = now_ms();
    uint64_t next_ka = 0, next_st = 0, next_pos = 0;
    uint32_t counter = 0;
    /* Beats on a microsecond schedule from the start: beat k is due at
     * t0 + k * period; the send time is that plus the jitter drawn for it. */
    int64_t beat_due_us = 0, beat_send_us = 0;
    uint32_t beat_k = 0;
    uint32_t rng = 0x12345678u;
    uint8_t bt[DJLINK_BEAT_PACKET_LEN];
    const int64_t t0_us = now_us();
    while (!s_stop) {
        const uint64_t now = now_ms();
        const unsigned long ms = c.start_ms + (c.playing ? (unsigned long)(now - t0) : 0ul);
        if (now >= next_ka) {
            send_to(fd, bcast, 50000, ka, sizeof(ka));
            next_ka = now + 1500;
        }
        if (now >= next_st) {
            build_status(&c, ms, counter++, st);
            send_to(fd, bcast, 50002, st, sizeof(st));
            next_st = now + 200;
        }
        if (c.position && !c.no_track && now >= next_pos) {
            build_position(&c, ms, pos);
            send_to(fd, bcast, 50001, pos, sizeof(pos));
            next_pos = now + 30;
        }
        if (c.beats && c.playing && !c.no_track) {
            const int64_t elapsed_us = now_us() - t0_us;
            while (elapsed_us >= beat_send_us) {
                build_beat(&c, beat_k, bt);
                send_to(fd, bcast, 50001, bt, sizeof(bt));
                beat_k++;
                beat_due_us += (int64_t)llround(60000000.0 / effective_bpm(&c));
                rng = rng * 1664525u + 1013904223u;
                const int jit = c.jitter_us ? (int)((rng >> 8) % (uint32_t)(2 * c.jitter_us + 1)) - c.jitter_us : 0;
                beat_send_us = beat_due_us + jit;
            }
        }
        usleep(1000);
    }
    close(fd);
    return 0;
}
