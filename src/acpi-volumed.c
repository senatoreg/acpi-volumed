#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <alsa/asoundlib.h>

static snd_mixer_selem_id_t *sid;
static int smixer_level = 0;
static int mono = 0;
static char *mix;
static char *card = "default";
static snd_mixer_t *handle;
static struct snd_mixer_selem_regopt smixer_options;
static snd_mixer_elem_t *elem;

int sock_fd;
const char *socketfile = "/var/run/acpid.socket";

int
get_handle() {
    int err;

    if ((err = snd_mixer_open(&handle, 0)) < 0) {
        error("Mixer %s open error: %s\n", card, snd_strerror(err));
        return err;
    }
    if (smixer_level == 0 && (err = snd_mixer_attach(handle, card)) < 0) {
        error("Mixer attach %s error: %s", card, snd_strerror(err));
        snd_mixer_close(handle);
        handle = NULL;
        return err;
    }
    if ((err = snd_mixer_selem_register(handle, smixer_level > 0 ? &smixer_options : NULL, NULL)) < 0) {
        error("Mixer register error: %s", snd_strerror(err));
        snd_mixer_close(handle);
        handle = NULL;
        return err;
    }
    err = snd_mixer_load(handle);
    if (err < 0) {
        error("Mixer %s load error: %s", card, snd_strerror(err));
        snd_mixer_close(handle);
        handle = NULL;
        return err;
    }
}

int
setup_alsa() {
    int err = 0;

    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_name(sid, mix);

    err = get_handle();

    elem = snd_mixer_find_selem(handle, sid);
    if (!elem) {
        error("Unable to find simple control '%s',%i\n", snd_mixer_selem_id_get_name(sid), snd_mixer_selem_id_get_index(sid));
        snd_mixer_close(handle);
        handle = NULL;
        return -ENOENT;
    } 

    mono=snd_mixer_selem_is_playback_mono(elem);

    return 0;
}

int
set_alsa_toggle_mute() {
    static int sw;

    if (snd_mixer_selem_has_playback_switch(elem) < 0) 
            return -ENOENT;

    if(snd_mixer_selem_get_playback_switch(elem, 0, &sw) < 0)
        return -ENOENT;
    if(snd_mixer_selem_set_playback_switch(elem, 0, !sw) < 0)
        return -ENOENT;

    if(!mono) {
        if(snd_mixer_selem_get_playback_switch(elem, 0, &sw) < 0)
            return -ENOENT;
        if(snd_mixer_selem_set_playback_switch(elem, 0, !sw) < 0)
            return -ENOENT;
    }

}

int
set_alsa_volume(long step) {
    long vol;
    long min;
    long max;

    if(snd_mixer_selem_get_playback_volume(elem, 0, &vol) < 0) {
        return -ENOENT;
    }

    if(snd_mixer_selem_get_playback_volume_range(elem, &min, &max) < 0) {
        return -ENOENT;
    }

    vol+=step;
    if (vol < min) vol=min;
    if (vol > max) vol=max;

    if(snd_mixer_selem_set_playback_volume(elem, 0, vol) < 0) {
        return -ENOENT;
    }

    if(!mono) {
        if(snd_mixer_selem_get_playback_volume(elem, 1, &vol) < 0) {
            return -ENOENT;
        }

        vol+=step;
        if(snd_mixer_selem_set_playback_volume(elem, 1, vol) < 0) {
            return -ENOENT;
        }
    }

    return 0;
}

close_alsa() {
    snd_mixer_close(handle);
}

acpi_open(const char* name) {
       int fd;
       int r;
       struct sockaddr_un addr;

    if (strnlen(name, sizeof(addr.sun_path)) > sizeof(addr.sun_path) - 1) {
        error("ud_connect(): "
            "socket filename longer than %zu characters: %s",
            sizeof(addr.sun_path) - 1, name);
        errno = EINVAL;
        return -1;
    }
    
       fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
       if (fd < 0) {
              return fd;
       }

       memset(&addr, 0, sizeof(addr));
       addr.sun_family = AF_UNIX;
       sprintf(addr.sun_path, "%s", name);
    /* safer: */
    /*strncpy(addr.sun_path, name, sizeof(addr.sun_path) - 1);*/

       r = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
       if (r < 0) {
              close(fd);
              return r;
       }

       return fd;
}

int
setup_acpi() {
    /* open the socket */
    sock_fd = acpi_open(socketfile);
    if (sock_fd < 0) {
        error("can't open socket %s: %s\n",
        socketfile, strerror(errno));
        return EXIT_FAILURE;
    }
    return 0;
}

int
close_acpi() {
    close(sock_fd);
}

#define MAX_BUFLEN 1024
static char *
read_line(int fd) {
    static char *buf;
    int buflen = 64;
    int i = 0;
    int r;
    int searching = 1;
    while (searching) {
        buf = realloc(buf, buflen);
        if (!buf) {
            fprintf(stderr, "ERR: malloc(%d): %s\n",
            buflen, strerror(errno));
            return NULL;
        }
        memset(buf+i, 0, buflen-i);
        while (i < buflen) {
            r = read(fd, buf+i, 1);
            if (r < 0) {
                /* we should do something with the data */
                fprintf(stderr, "ERR: read(): %s\n",
                strerror(errno));
                return NULL;
            } else if (r == 0) {
                /* signal this in an almost standard way */
                errno = EPIPE;
                return NULL;
            } else if (r == 1) {
                /* scan for a newline */
                if (buf[i] == '\n') {
                    searching = 0;
                    buf[i] = '\0';
                    break;
                }
                i++;
            }
        }
        if (buflen >= MAX_BUFLEN) {
            break;
        }
            buflen *= 2;
    }
    return buf;
}

int
main(int argc, char** argv, char** envp) {
    int pid;
    int err;
    int step;
    int mix_len;

    step = 1;

    if (argc >= 2) {
        mix_len=strlen(argv[1])+1;
        mix=malloc(mix_len);
        strcpy(mix,argv[1]);
    } else {
        mix=malloc(7);
        strcpy(mix,"Master");
    } 

    if (argc >= 3) {
        step = atoi(argv[2]);
    } 

    pid = fork();
    if (pid == -1)
        return -1;
    else if (pid != 0)
         return 0;

    if (setsid() == -1)
        return -1;

    if (chdir("/") == -1)
        return -1;

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    if ( (err=setup_alsa()) != 0 ) {
        return err;
    }
        
    if ( (err=setup_acpi()) != 0 ) {
        return err;
    }
        
    while (1) {
        char *event;
        /* read and handle an event */
        event = read_line(sock_fd);
        if (event) { 
            if ((err=strncmp(event,"button/volumedown VOLDN",23)) == 0) {
                if ((err=set_alsa_volume(-step)) < 0) break;
            } else if ((err=strncmp(event,"button/volumeup VOLUP",21)) == 0) {
                if ((err=set_alsa_volume(step)) < 0) break;
            } else if ((err=strncmp(event,"button/mute MUTE",16)) == 0) {
                if ((err=set_alsa_toggle_mute()) < 0) break;
            } else if ((err=strncmp(event,"button/f20 F20",14)) == 0) {
            }
        }
    }

    close_acpi();
    close_alsa();

    free(mix);

}
