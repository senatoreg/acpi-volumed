#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <error.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <alsa/asoundlib.h>

static char *default_playback = "Master";
static char *default_capture = "Capture";
static snd_mixer_selem_id_t *sid_playback;
static snd_mixer_selem_id_t *sid_capture;
static int smixer_level = 0;
static int mono_playback = 0;
static int mono_capture = 0;
static char *playback;
static char *capture;
static char *card = "default";
static snd_mixer_t *handle;
static struct snd_mixer_selem_regopt smixer_options;
static snd_mixer_elem_t *elem_playback;
static snd_mixer_elem_t *elem_capture;

int sock_fd;
const char *socketfile = "/var/run/acpid.socket";

__asm__(".symver snd_mixer_open,snd_mixer_open@ALSA_0.9");
int
get_handle() {
    int err;

    if ((err = snd_mixer_open(&handle, 0)) < 0) {
        error(0, 0, "Mixer %s open error: %s\n", card, snd_strerror(err));
        return err;
    }
    if (smixer_level == 0 && (err = snd_mixer_attach(handle, card)) < 0) {
        error(0, 0, "Mixer attach %s error: %s", card, snd_strerror(err));
        snd_mixer_close(handle);
        handle = NULL;
        return err;
    }
    if ((err = snd_mixer_selem_register(handle, smixer_level > 0 ? &smixer_options : NULL, NULL)) < 0) {
        error(0, 0, "Mixer register error: %s", snd_strerror(err));
        snd_mixer_close(handle);
        handle = NULL;
        return err;
    }
    err = snd_mixer_load(handle);
    if (err < 0) {
        error(0, 0, "Mixer %s load error: %s", card, snd_strerror(err));
        snd_mixer_close(handle);
        handle = NULL;
        return err;
    }
    return err;
}

int
setup_alsa() {
    int err = 0;

    err = get_handle();

    snd_mixer_selem_id_alloca(&sid_playback);
    snd_mixer_selem_id_set_name(sid_playback, playback);

    elem_playback = snd_mixer_find_selem(handle, sid_playback);
    if (!elem_playback) {
        error(0, 0, "Unable to find simple control '%s',%i\n", snd_mixer_selem_id_get_name(sid_playback), snd_mixer_selem_id_get_index(sid_playback));
        snd_mixer_close(handle);
        handle = NULL;
        return -ENOENT;
    } 

    mono_playback=snd_mixer_selem_is_playback_mono(elem_playback);

    snd_mixer_selem_id_alloca(&sid_capture);
    snd_mixer_selem_id_set_name(sid_capture, capture);

    elem_capture = snd_mixer_find_selem(handle, sid_capture);
    if (!elem_capture) {
        error(0, 0, "Unable to find simple control '%s',%i\n", snd_mixer_selem_id_get_name(sid_capture), snd_mixer_selem_id_get_index(sid_capture));
        snd_mixer_close(handle);
        handle = NULL;
        return -ENOENT;
    } 

    mono_capture=snd_mixer_selem_is_capture_mono(elem_capture);

    return 0;
}

int
set_alsa_toggle_playback_mute() {
    static int sw;
    int err;

    if ((err = snd_mixer_selem_has_playback_switch(elem_playback)) < 0) {
      error(0, 0, "Mixer has playback switch error: %s", snd_strerror(err));
      return err;
    }

    if((err = snd_mixer_selem_get_playback_switch(elem_playback, 0, &sw)) < 0) {
      error(0, 0, "Mixer get playback 0 switch error: %s", snd_strerror(err));
      return err;
    }
    if((err = snd_mixer_selem_set_playback_switch(elem_playback, 0, !sw)) < 0) {
      error(0, 0, "Mixer set playback 0 switch error: %s", snd_strerror(err));
      return err;
    }
    
    if(!mono_playback) {
      if((err = snd_mixer_selem_get_playback_switch(elem_playback, 1, &sw)) < 0) {
	error(0, 0, "Mixer get playback 1 switch error: %s", snd_strerror(err));
	return err;
      }
	if((err = snd_mixer_selem_set_playback_switch(elem_playback, 1, !sw)) < 0) {
	  error(0, 0, "Mixer set playback 1 switch error: %s", snd_strerror(err));
	  return err;
      }
    }
    return 0;

}

int
set_alsa_toggle_capture_mute() {
    static int sw;
    int err;

    if ((err = snd_mixer_selem_has_capture_switch(elem_capture)) < 0) {
      error(0, 0, "Mixer has playback switch error: %s", snd_strerror(err));
      return err;
    }
    
    if((err = snd_mixer_selem_get_capture_switch(elem_capture, 0, &sw)) < 0) {
      error(0, 0, "Mixer get capture 0 switch error: %s", snd_strerror(err));
      return err;
    }
    if((err = snd_mixer_selem_set_capture_switch(elem_capture, 0, !sw)) < 0) {
      error(0, 0, "Mixer set capture 0 switch error: %s", snd_strerror(err));
      return err;
    }

    if(!mono_capture) {
      if((err = snd_mixer_selem_get_capture_switch(elem_capture, 1, &sw)) < 0) {
	error(0, 0, "Mixer get capture 1 switch error: %s", snd_strerror(err));
	return err;
      }
      if((err = snd_mixer_selem_set_capture_switch(elem_capture, 1, !sw)) < 0) {
	error(0, 0, "Mixer set capture 1 switch error: %s", snd_strerror(err));
	return err;
      }
    }
    return 0;

}

int
set_alsa_playback_volume(long step) {
    long vol;
    long min;
    long max;

    if(snd_mixer_selem_get_playback_volume(elem_playback, 0, &vol) < 0) {
        return -ENOENT;
    }

    if(snd_mixer_selem_get_playback_volume_range(elem_playback, &min, &max) < 0) {
        return -ENOENT;
    }

    vol+=step;
    if (vol < min) vol=min;
    if (vol > max) vol=max;

    if(snd_mixer_selem_set_playback_volume(elem_playback, 0, vol) < 0) {
        return -ENOENT;
    }

    if(!mono_playback) {
        if(snd_mixer_selem_get_playback_volume(elem_playback, 1, &vol) < 0) {
            return -ENOENT;
        }

        vol+=step;
        if(snd_mixer_selem_set_playback_volume(elem_playback, 1, vol) < 0) {
            return -ENOENT;
        }
    }

    return 0;
}

void close_alsa() {
    snd_mixer_close(handle);
}

int acpi_open(const char* name) {
       int fd;
       int r;
       struct sockaddr_un addr;

    if (strnlen(name, sizeof(addr.sun_path)) > sizeof(addr.sun_path) - 1) {
        error(0, 0, "ud_connect(): "
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
        error(0, 0, "can't open socket %s: %s\n",
        socketfile, strerror(errno));
        return EXIT_FAILURE;
    }
    return 0;
}

int
close_acpi() {
    return close(sock_fd);
}

#define MAX_BUFLEN 128

int
main(int argc, char** argv, char** envp) {
    int pid, err, step, c;
    int playback_len, free_playback=0;
    struct pollfd *pfds;
    char event[MAX_BUFLEN];

    step = 1;
    playback=default_playback;
    capture=default_capture;

    while ((c = getopt (argc, argv, "d:s:c:")) != -1)
        switch (c)
            {
            case 'd':
                playback = optarg;
                break;
            case 'c':
                capture = optarg;
                break;
            case 's':
                step = atoi(optarg);
                break;
            default:
                break;
            }

    /*
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
    */
    
    if ( (err=setup_alsa()) != 0 ) {
        return err;
    }
        
    if ( (err=setup_acpi()) != 0 ) {
        return err;
    }
        
    pfds = malloc(sizeof(struct pollfd));

    pfds->fd = sock_fd;
    pfds->events = POLLIN;

    while (1) {
	err = poll(pfds, 1, -1);

	if (pfds->revents && POLLIN) {
	    err = read(pfds->fd, event, MAX_BUFLEN);
	    if (err > 0) {
		if ((err=strncmp(event,"button/volumedown VOLDN",23)) == 0) {
		    if ((err=set_alsa_playback_volume(-step)) < 0) break;
		} else if ((err=strncmp(event,"button/volumeup VOLUP",21)) == 0) {
		    if ((err=set_alsa_playback_volume(step)) < 0) break;
		} else if ((err=strncmp(event,"button/mute MUTE",16)) == 0) {
		    if ((err=set_alsa_toggle_playback_mute()) < 0) break;
		} else if ((err=strncmp(event,"button/f20 F20",14)) == 0) {
		    if ((err=set_alsa_toggle_capture_mute()) < 0) break;
		}
	    }
	}
    }

    close_acpi();
    close_alsa();

    return err;
}
