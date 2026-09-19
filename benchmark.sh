#!/data/data/com.termux/files/usr/bin/bash
# Benchmark: SLAC vs FLAC vs Opus

SLAC_CLI="./build/slacodec-cli"
OPUS_BITRATE=128

declare -a TRACKS=(
    "Zericxxn - Raya.wav"
    "Loser - Tame Impala.wav"
    "C & C Music Factory - Gonna Make You Sweat (Everybody Dance Now) (feat. Freedom Williams).wav"
)

measure() {
    local s=$(date +%s.%N)
    "$@" > /dev/null 2>&1
    local e=$(date +%s.%N)
    awk "BEGIN {printf \"%.2f\", $e - $s}"
}

echo "======================================================"
echo "  SLAC vs FLAC vs Opus — Benchmark"
echo "======================================================"

for wav in "${TRACKS[@]}"; do
    echo ""
    echo "── Track: $wav ──"
    wav_size=$(stat -c%s "$wav")
    awk "BEGIN {printf \"WAV original: %.1f MB\n\", $wav_size/1048576}"

    # SLAC
    printf "  SLAC encode... "
    t_se=$(measure $SLAC_CLI encode "$wav" tmp.slac)
    slac_size=$(stat -c%s tmp.slac)
    echo "$t_se s"
    printf "  SLAC decode... "
    t_sd=$(measure $SLAC_CLI decode tmp.slac tmp_s.wav)
    echo "$t_sd s"

    # FLAC
    printf "  FLAC encode... "
    t_fe=$(measure flac -f "$wav" -o tmp.flac)
    flac_size=$(stat -c%s tmp.flac)
    echo "$t_fe s"
    printf "  FLAC decode... "
    t_fd=$(measure flac -df tmp.flac -o tmp_f.wav)
    echo "$t_fd s"

    # Opus
    printf "  Opus encode ($OPUS_BITRATE kbps)... "
    t_oe=$(measure opusenc "$wav" tmp.opus --bitrate $OPUS_BITRATE)
    opus_size=$(stat -c%s tmp.opus)
    echo "$t_oe s"
    printf "  Opus decode... "
    t_od=$(measure opusdec tmp.opus tmp_o.wav)
    echo "$t_od s"

    echo ""
    printf "  %-6s %9s %8s %9s %9s\n" "Format" "Size(MB)" "Ratio" "Enc(s)" "Dec(s)"
    awk -v w=$wav_size \
        -v ss=$slac_size -v fs=$flac_size -v os=$opus_size \
        -v tse=$t_se -v tsd=$t_sd -v tfe=$t_fe -v tfd=$t_fd -v toe=$t_oe -v tod=$t_od \
        'BEGIN {
            printf "  %-6s %9.1f %7.1f%% %9s %9s\n", "WAV",  w/1048576, 100.0, "-", "-"
            printf "  %-6s %9.1f %7.1f%% %9.2f %9.2f\n", "SLAC", ss/1048576, 100.0*ss/w, tse, tsd
            printf "  %-6s %9.1f %7.1f%% %9.2f %9.2f\n", "FLAC", fs/1048576, 100.0*fs/w, tfe, tfd
            printf "  %-6s %9.1f %7.1f%% %9.2f %9.2f\n", "Opus", os/1048576, 100.0*os/w, toe, tod
        }'

    rm -f tmp.slac tmp.flac tmp.opus tmp_s.wav tmp_f.wav tmp_o.wav
done

echo ""
echo "======================================================"
echo "  Benchmark completo!"
echo "======================================================"
