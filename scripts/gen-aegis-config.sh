#!/bin/sh

# path to binary
BINARY_PATH="/usr/sbin/ohm-session-agent"

ERROR=1
OK=0

DEBUG=""

error() {
    echo "$*" 1>&2
}


debug() {
    [ -n "$DEBUG" ] && echo "$*" 1>&2 || :
}


emit_prefix() {
    echo '<aegis>'
}

emit_suffix() {
    echo '</aegis>'
}


emit_request_prefix() {
    echo '  <request>'
}

emit_request_suffix() {
    echo "    <for path=\"$BINARY_PATH\"/>"
    echo '  </request>'
}


emit_provide_prefix() {
    echo '  <provide>'
}

emit_provide_suffix() {
    echo '  </provide>'
}


emit_credential() {
    credential="$1"
    comment="$2"

    debug "=> emit_credentials $*"

    if [ -n "$comment" ]; then
        echo "    # $comment"
    fi

    echo "    <credential name=\"$credential\"/>"

    debug "<= emit_crenentials"
    return $OK
}

emit_plugin_credentials() {
    type="$1"
    plugin="${2%.credentials}"; plugin="${plugin##*/}"
    plugin_status=$OK

    debug "=> emit_plugin_credentials $*"

    whom="$plugin"
    cat $2 | grep -v "^#" | \
             grep "^$type" | while read type credential extra; do
        debug "emit_plugin_credentials: read line \"$credential $extra\""

        # skip empty lines and comments
        if test -z "$credential"; then
            continue
        fi

        case $credential in
            '#'*) continue;;
        esac

        # skip invalid lines
        if test -n "$extra"; then
            error "invalid input [$credential $extra] from $cfg"
            plugin_status=$ERROR
            continue
        fi

        emit_credential "$credential" "$whom" || return $ERROR
        whom=""
    done

    debug "<= emit_plugin_credentials"
    return $plugin_status
}



#########################
# main script
#

if [ "$1" = "-d" ]; then
    DEBUG=yes
    shift
fi

if [ "$1" = "--path" ]; then
    BINARY_PATH="$2"
    shift 2
fi

CONFIG_DIR="${1:-/etc/ohm/plugins.d}"

status=$OK

emit_prefix

# emit requested credentials
emit_request_prefix

for cfg in $CONFIG_DIR/*.credentials; do
    if test -f $cfg; then
        emit_plugin_credentials request $cfg || status=$ERROR
    fi
done

emit_request_suffix

# emit provided credentials
# emit_provide_prefix
# for cfg in $CONFIG_DIR/*.credentials; do
#     if test -f $cfg; then
#         emit_plugin_credentials provide $cfg || status=$ERROR
#     fi
# done
#
# emit_provide_suffix

emit_suffix

exit $status
