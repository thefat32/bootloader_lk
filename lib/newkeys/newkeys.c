#include <err.h>
#include <bits.h>
#include <list.h>
#include <debug.h>
#include <string.h>
#include <malloc.h>
#include <dev/newkeys.h>

typedef struct {
    struct list_node node;
    uint16_t code;
    uint16_t value;
} newkey_event_t;

static struct list_node event_queue;
static struct list_node event_sources;

void newkeys_init(void)
{
    list_initialize(&event_queue);
    list_initialize(&event_sources);
}

int newkeys_post_event(uint16_t code, int16_t value)
{
    int i;
    uint8_t seq[3];
    uint8_t seqsz = 0;

    // we do not report up events
    if (!value) return NO_ERROR;

    if (code >= MAX_KEYS) {
        return ERR_INVALID_ARGS;
    }

    if (code<=0xff) {
        seq[seqsz++] = code;
    } else {
        switch (code) {
            case KEY_VOLUMEUP:
                seq[seqsz++] = 0x1b;
                seq[seqsz++] = 0x5b;
                seq[seqsz++] = 0x41;
                break;

            case KEY_VOLUMEDOWN:
                seq[seqsz++] = 0x1b;
                seq[seqsz++] = 0x5b;
                seq[seqsz++] = 0x42;
                break;

            case KEY_BACK:
                seq[seqsz++] = 0x1b;
                seq[seqsz++] = 0x5b;
                seq[seqsz++] = 0x44;
                break;
        }
    }

    for (i=0; i<seqsz; i++) {
        // create event
        newkey_event_t *event = malloc(sizeof(newkey_event_t));
        event->code = seq[i];
        event->value = value;

        // add event
        list_add_tail(&event_queue, &event->node);
    }

    return NO_ERROR;
}

int newkeys_poll(void)
{
    newkey_event_source_t *source;
    list_for_every_entry(&event_sources, source, newkey_event_source_t, node) {
        // update time
        time_t current = current_time();
        source->delta = current-source->last;
        source->last = current;

        source->poll(source);
    }

    return 0;
}

void newkeys_add_source(newkey_event_source_t *source)
{
    int i;

    source->last = INFINITE_TIME;

    for (i=0; i<MAX_KEYS; i++) {
        source->keymap[i].time = 0;
        source->keymap[i].repeat = false;
        source->keymap[i].longpress = false;
        source->keymap[i].state = KEYSTATE_RELEASED;
    }

    list_add_tail(&event_sources, &source->node);
}

void newkeys_delete_all_events(void)
{
    while (newkeys_has_event()) {
        newkey_event_t *event = list_remove_tail_type(&event_queue, newkey_event_t, node);
        free(event);
    }
}

int newkeys_get_next_event(uint16_t *code, uint16_t *value)
{
    // wait for next event
    if (!newkeys_has_event())
        return ERR_NOT_READY;

    // get event
    newkey_event_t *event = list_remove_head_type(&event_queue, newkey_event_t, node);

    // set result code
    *code = event->code;
    *value = event->value;

    // free event
    free(event);

    return NO_ERROR;
}

int newkeys_has_event(void)
{
    return !list_is_empty(&event_queue);
}

int newkeys_set_report_key(newkey_event_source_t *source, uint16_t code, uint16_t value)
{
    // update key time
    source->keymap[code].time+=source->delta;

    switch (source->keymap[code].state) {
        case KEYSTATE_RELEASED:
            if (value) {
                // change to pressed
                if (source->keymap[code].enable_longpress) {
                    source->keymap[code].time=0;
                    source->keymap[code].state = KEYSTATE_LONGPRESS_WAIT;
                } else {
                    newkeys_post_event(code, 1);
                    source->keymap[code].time=0;
                    source->keymap[code].state = KEYSTATE_PRESSED;
                }
            }
            break;

        case KEYSTATE_PRESSED:
            if (!value && source->keymap[code].time>50) {
                // change to released
                source->keymap[code].time=0;
                source->keymap[code].repeat=false;
                source->keymap[code].longpress = false;
                source->keymap[code].state = KEYSTATE_RELEASED;
                newkeys_post_event(code, 0);
            }

            // key repeat
            else if ((source->keymap[code].repeat && source->keymap[code].time>=200) || source->keymap[code].time>=500) {
                newkeys_post_event(code, 1);
                source->keymap[code].time=0;
                source->keymap[code].repeat=true;
            }
            break;

        case KEYSTATE_LONGPRESS_WAIT:
            if (value) {
                if (!source->keymap[code].longpress && source->keymap[code].time>=500) {
                    if (keys_get_state(KEY_VOLUMEDOWN)) {
                        // report 's'
                        newkeys_post_event(0x73, 1);
                        newkeys_post_event(0x73, 0);
                    } else if (keys_get_state(KEY_VOLUMEUP)) {
                        // report 'e'
                        newkeys_post_event(0x65, 1);
                        newkeys_post_event(0x65, 0);
                    } else {
                        // report spacebar
                        newkeys_post_event(32, 1);
                        newkeys_post_event(32, 0);
                    }

                    source->keymap[code].longpress=true;
                }
            }

            else {
                if (!source->keymap[code].longpress) {
                    // we supressed down, so report it now
                    newkeys_post_event(code, 1);
                    source->keymap[code].state = KEYSTATE_LONGPRESS_RELEASE;
                }

                else if (source->keymap[code].time>50) {
                    // we reported a longpress already
                    source->keymap[code].time=0;
                    source->keymap[code].repeat=false;
                    source->keymap[code].longpress = false;
                    source->keymap[code].state = KEYSTATE_RELEASED;
                }
            }
            break;

        case KEYSTATE_LONGPRESS_RELEASE:
            // change to released
            source->keymap[code].time=0;
            source->keymap[code].repeat=false;
            source->keymap[code].longpress = false;
            source->keymap[code].state = KEYSTATE_RELEASED;
            newkeys_post_event(code, 0);
            break;

        default:
            ASSERT(0);
    }

    return NO_ERROR;
}