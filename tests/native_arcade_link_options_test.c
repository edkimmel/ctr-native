#include "platform/native_arcade_link_options.h"

#include "platform/native_sha256.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define ARGC(args) ((int)(sizeof(args) / sizeof((args)[0])))

/* SHA-256 of "CTRN arcade-link fixture bot rules v1" (37 bytes), from sha256sum. */
static const uint8_t k_botRulesDigest[NATIVE_SHA256_DIGEST_BYTES] = {
	0x26, 0x68, 0x46, 0x70, 0x32, 0x49, 0x6b, 0x64, 0xfc, 0x57, 0x02, 0xb7, 0x0d, 0x0c, 0x26, 0x04,
	0x8a, 0xa6, 0x33, 0x46, 0x07, 0x8c, 0xd3, 0x46, 0x7f, 0xdf, 0xbb, 0x54, 0xc3, 0xf3, 0xea, 0x73,
};

static void Sentinel(struct NativeArcadeLinkOptions *options)
{
	memset(options, 0xA5, sizeof(*options));
}

/* ApplyArgs must fail and leave a sentinel-filled struct byte-identical. */
static int RejectsUntouched(int argc, char *argv[])
{
	struct NativeArcadeLinkOptions options;
	struct NativeArcadeLinkOptions snapshot;

	Sentinel(&options);
	snapshot = options;
	if (NativeArcadeLinkOptions_ApplyArgs(argc, argv, &options))
	{
		return 0;
	}
	return memcmp(&options, &snapshot, sizeof(options)) == 0;
}

/* The same, but from defaults, so only the post-scan rules can reject. */
static int RejectsFromDefaults(int argc, char *argv[])
{
	struct NativeArcadeLinkOptions options;
	struct NativeArcadeLinkOptions snapshot;

	NativeArcadeLinkOptions_SetDefaults(&options);
	snapshot = options;
	if (NativeArcadeLinkOptions_ApplyArgs(argc, argv, &options))
	{
		return 0;
	}
	return memcmp(&options, &snapshot, sizeof(options)) == 0;
}

static void FillIdentity(struct NativeIdentityV1 *identity, uint8_t buildSeed, uint8_t contentSeed)
{
	for (uint32_t i = 0; i < NATIVE_IDENTITY_DIGEST_BYTES; i++)
	{
		identity->build[i] = (uint8_t)(buildSeed + i);
		identity->content[i] = (uint8_t)(contentSeed + (i * 3u));
	}
}

static int TestSetDefaults(void)
{
	struct NativeArcadeLinkOptions options;
	struct NativeArcadeLinkOptions zero;

	NativeArcadeLinkOptions_SetDefaults(NULL);
	Sentinel(&options);
	NativeArcadeLinkOptions_SetDefaults(&options);
	memset(&zero, 0, sizeof(zero));
	CHECK(memcmp(&options, &zero, sizeof(options)) == 0);
	CHECK(options.enabled == 0);
	CHECK(options.localRole == 0);
	CHECK(options.localPort == 0);
	CHECK(options.peerCount == 0);
	CHECK(options.preview == NATIVE_ARCADE_LINK_PREVIEW_NONE);
	return 0;
}

static int TestParsePeer(void)
{
	static const struct
	{
		const char *text;
		uint32_t ipv4;
		uint16_t port;
	} accepted[] = {
		{"127.0.0.1:48000", UINT32_C(0x7F000001), 48000u},
		{"10.0.0.2:1", UINT32_C(0x0A000002), 1u},
		{"255.255.255.255:65535", UINT32_C(0xFFFFFFFF), 65535u},
		{"0.0.0.0:80", UINT32_C(0x00000000), 80u},
		{"192.168.001.020:00080", UINT32_C(0xC0A80114), 80u},
	};
	static const char *const rejected[] = {
		"256.0.0.1:1",
		"1.2.3:4",
		"1.2.3.4",
		"1.2.3.4:",
		"1.2.3.4:0",
		"1.2.3.4:65536",
		"host:1",
		" 1.2.3.4:5",
		"1.2.3.4:5x",
		"1..2.3:4",
		"-1.2.3.4:5",
		"+1.2.3.4:5",
		"1.2.3.4:+5",
		"1.2.3.4: 5",
		"1.2.3.4 :5",
		"1.2.3.4:5 ",
		"1.2.3.4.5:6",
		"1.2.3.4::5",
		"1.2.3.4:5:6",
		"0001.2.3.4:5",
		"1.2.3.4:000001",
		"1.2.3.4:99999",
		"",
		":5",
		"localhost:48000",
	};
	struct NativeArcadeLinkPeer peer;
	struct NativeArcadeLinkPeer snapshot;

	for (size_t i = 0; i < sizeof(accepted) / sizeof(accepted[0]); i++)
	{
		memset(&peer, 0xA5, sizeof(peer));
		CHECK(NativeArcadeLinkOptions_ParsePeer(accepted[i].text, &peer));
		CHECK(peer.ipv4 == accepted[i].ipv4);
		CHECK(peer.port == accepted[i].port);
		CHECK(peer.reserved == 0);
	}
	for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++)
	{
		memset(&peer, 0xA5, sizeof(peer));
		snapshot = peer;
		if (NativeArcadeLinkOptions_ParsePeer(rejected[i], &peer))
		{
			fprintf(stderr, "accepted malformed peer '%s'\n", rejected[i]);
			return 1;
		}
		CHECK(memcmp(&peer, &snapshot, sizeof(peer)) == 0);
	}
	CHECK(!NativeArcadeLinkOptions_ParsePeer(NULL, &peer));
	CHECK(!NativeArcadeLinkOptions_ParsePeer("127.0.0.1:48000", NULL));
	return 0;
}

static int TestPreviewNames(void)
{
	static const struct
	{
		const char *name;
		uint32_t preview;
	} names[] = {
		{"title", NATIVE_ARCADE_LINK_PREVIEW_TITLE},
		{"lobby", NATIVE_ARCADE_LINK_PREVIEW_LOBBY_WAITING},
		{"lobby-connecting", NATIVE_ARCADE_LINK_PREVIEW_LOBBY_CONNECTING},
		{"lobby-rejected", NATIVE_ARCADE_LINK_PREVIEW_LOBBY_REJECTED},
		{"match-found", NATIVE_ARCADE_LINK_PREVIEW_MATCH_FOUND},
		{"results", NATIVE_ARCADE_LINK_PREVIEW_RESULTS_FINISHED},
		{"results-timeout", NATIVE_ARCADE_LINK_PREVIEW_RESULTS_PEER_TIMEOUT},
		{"results-desync", NATIVE_ARCADE_LINK_PREVIEW_RESULTS_DESYNC},
		{"results-link-error", NATIVE_ARCADE_LINK_PREVIEW_RESULTS_LINK_ERROR},
		{"rematch", NATIVE_ARCADE_LINK_PREVIEW_REMATCH_WAIT},
		{"exit", NATIVE_ARCADE_LINK_PREVIEW_EXIT},
		{"exit-opponent-left", NATIVE_ARCADE_LINK_PREVIEW_EXIT_OPPONENT_LEFT},
	};
	static const char *const rejected[] = {
		"none", "", "Title", "TITLE", "lobby ", " lobby", "lobby-waiting", "result", "results-finished",
		"exit-opponent", "rematch-wait", "0", "1",
	};
	uint32_t preview;

	CHECK(sizeof(names) / sizeof(names[0]) == 12u);
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
	{
		CHECK(names[i].preview == (uint32_t)(i + 1u));
		preview = 0xDEADBEEFu;
		CHECK(NativeArcadeLinkOptions_ParsePreview(names[i].name, &preview));
		CHECK(preview == names[i].preview);
		CHECK(strcmp(NativeArcadeLinkOptions_PreviewName(names[i].preview), names[i].name) == 0);
		CHECK(NativeArcadeLinkOptions_ParsePreview(NativeArcadeLinkOptions_PreviewName(preview), &preview));
		CHECK(preview == names[i].preview);
	}
	for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++)
	{
		preview = 0xDEADBEEFu;
		CHECK(!NativeArcadeLinkOptions_ParsePreview(rejected[i], &preview));
		CHECK(preview == 0xDEADBEEFu);
	}
	preview = 0xDEADBEEFu;
	CHECK(!NativeArcadeLinkOptions_ParsePreview(NULL, &preview));
	CHECK(preview == 0xDEADBEEFu);
	CHECK(!NativeArcadeLinkOptions_ParsePreview("title", NULL));
	CHECK(strcmp(NativeArcadeLinkOptions_PreviewName(NATIVE_ARCADE_LINK_PREVIEW_NONE), "none") == 0);
	CHECK(strcmp(NativeArcadeLinkOptions_PreviewName(13u), "unknown") == 0);
	CHECK(strcmp(NativeArcadeLinkOptions_PreviewName(0xFFFFFFFFu), "unknown") == 0);
	return 0;
}

static int TestApplyArgsValid(void)
{
	char *noArgs[] = {"ctr_native"};
	char *cab1[] = {"ctr_native", "--render-scale", "2", "--arcade-link", "cab1", "--fullscreen", "--arcade-link-port", "48000",
		"--arcade-link-peer", "10.0.0.2:48001", "--capture-frame", "30=a.bmp", "--arcade-link-peer", "127.0.0.1:48002"};
	char *cab2[] = {"ctr_native", "--arcade-link-peer", "192.168.1.10:1", "--arcade-link-port", "65535", "--arcade-link", "cab2"};
	char *previewOnly[] = {"ctr_native", "--windowed", "--arcade-link-preview", "results-desync", "--exit-after-frame", "31"};
	char *eightPeers[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link-port", "1", "--arcade-link-peer", "1.0.0.1:1",
		"--arcade-link-peer", "1.0.0.2:2", "--arcade-link-peer", "1.0.0.3:3", "--arcade-link-peer", "1.0.0.4:4",
		"--arcade-link-peer", "1.0.0.5:5", "--arcade-link-peer", "1.0.0.6:6", "--arcade-link-peer", "1.0.0.7:7",
		"--arcade-link-peer", "1.0.0.8:8"};
	char *unrelatedOnly[] = {"ctr_native", "--render-scale", "2", "--fullscreen", "--arcade-link=cab1", "-arcade-link"};
	struct NativeArcadeLinkOptions options;
	struct NativeArcadeLinkOptions zero;

	memset(&zero, 0, sizeof(zero));

	NativeArcadeLinkOptions_SetDefaults(&options);
	CHECK(NativeArcadeLinkOptions_ApplyArgs(ARGC(noArgs), noArgs, &options));
	CHECK(memcmp(&options, &zero, sizeof(options)) == 0);
	CHECK(NativeArcadeLinkOptions_ApplyArgs(0, NULL, &options));
	CHECK(memcmp(&options, &zero, sizeof(options)) == 0);
	CHECK(NativeArcadeLinkOptions_ApplyArgs(ARGC(unrelatedOnly), unrelatedOnly, &options));
	CHECK(memcmp(&options, &zero, sizeof(options)) == 0);

	NativeArcadeLinkOptions_SetDefaults(&options);
	CHECK(NativeArcadeLinkOptions_ApplyArgs(ARGC(cab1), cab1, &options));
	CHECK(options.enabled == 1);
	CHECK(options.localRole == NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN);
	CHECK(options.localPort == 48000u);
	CHECK(options.peerCount == 2u);
	CHECK(options.peers[0].ipv4 == UINT32_C(0x0A000002));
	CHECK(options.peers[0].port == 48001u);
	CHECK(options.peers[0].reserved == 0);
	CHECK(options.peers[1].ipv4 == UINT32_C(0x7F000001));
	CHECK(options.peers[1].port == 48002u);
	CHECK(options.peers[1].reserved == 0);
	for (uint32_t i = 2; i < NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS; i++)
	{
		CHECK(options.peers[i].ipv4 == 0 && options.peers[i].port == 0 && options.peers[i].reserved == 0);
	}
	CHECK(options.preview == NATIVE_ARCADE_LINK_PREVIEW_NONE);

	NativeArcadeLinkOptions_SetDefaults(&options);
	CHECK(NativeArcadeLinkOptions_ApplyArgs(ARGC(cab2), cab2, &options));
	CHECK(options.enabled == 1);
	CHECK(options.localRole == NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN);
	CHECK(options.localPort == 65535u);
	CHECK(options.peerCount == 1u);
	CHECK(options.peers[0].ipv4 == UINT32_C(0xC0A8010A));
	CHECK(options.peers[0].port == 1u);
	CHECK(options.preview == NATIVE_ARCADE_LINK_PREVIEW_NONE);

	NativeArcadeLinkOptions_SetDefaults(&options);
	CHECK(NativeArcadeLinkOptions_ApplyArgs(ARGC(previewOnly), previewOnly, &options));
	CHECK(options.enabled == 0);
	CHECK(options.localRole == 0);
	CHECK(options.localPort == 0);
	CHECK(options.peerCount == 0);
	CHECK(options.preview == NATIVE_ARCADE_LINK_PREVIEW_RESULTS_DESYNC);

	NativeArcadeLinkOptions_SetDefaults(&options);
	CHECK(NativeArcadeLinkOptions_ApplyArgs(ARGC(eightPeers), eightPeers, &options));
	CHECK(options.peerCount == NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS);
	for (uint32_t i = 0; i < NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS; i++)
	{
		CHECK(options.peers[i].ipv4 == (UINT32_C(0x01000000) | (i + 1u)));
		CHECK(options.peers[i].port == (uint16_t)(i + 1u));
	}
	return 0;
}

static int TestApplyArgsErrors(void)
{
	/* Missing values: end of argv, NULL, or a next argument starting with '-'. */
	char *linkMissing[] = {"ctr_native", "--arcade-link"};
	char *linkDash[] = {"ctr_native", "--arcade-link", "--arcade-link-port", "1", "--arcade-link-peer", "1.2.3.4:5"};
	char *linkNull[] = {"ctr_native", "--arcade-link", NULL};
	char *portMissing[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link-peer", "1.2.3.4:5", "--arcade-link-port"};
	char *portDash[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link-port", "-1", "--arcade-link-peer", "1.2.3.4:5"};
	char *peerMissing[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link-port", "1", "--arcade-link-peer"};
	char *peerDash[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link-port", "1", "--arcade-link-peer", "--fullscreen"};
	char *previewMissing[] = {"ctr_native", "--arcade-link-preview"};
	char *previewDash[] = {"ctr_native", "--arcade-link-preview", "--windowed"};
	char *previewNull[] = {"ctr_native", "--arcade-link-preview", NULL};
	/* --arcade-link: bad value or given twice. */
	char *linkBadValue[] = {"ctr_native", "--arcade-link", "cab3", "--arcade-link-port", "1", "--arcade-link-peer", "1.2.3.4:5"};
	char *linkUpper[] = {"ctr_native", "--arcade-link", "CAB1", "--arcade-link-port", "1", "--arcade-link-peer", "1.2.3.4:5"};
	char *linkTwice[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link", "cab1", "--arcade-link-port", "1",
		"--arcade-link-peer", "1.2.3.4:5"};
	char *linkTwiceMixed[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link", "cab2", "--arcade-link-port", "1",
		"--arcade-link-peer", "1.2.3.4:5"};
	/* --arcade-link-port: bad value or given twice. */
	char *portZero[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link-port", "0", "--arcade-link-peer", "1.2.3.4:5"};
	char *portHigh[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link-port", "65536", "--arcade-link-peer", "1.2.3.4:5"};
	char *portLong[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link-port", "000001", "--arcade-link-peer", "1.2.3.4:5"};
	char *portAlpha[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link-port", "48k", "--arcade-link-peer", "1.2.3.4:5"};
	char *portPlus[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link-port", "+1", "--arcade-link-peer", "1.2.3.4:5"};
	char *portEmpty[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link-port", "", "--arcade-link-peer", "1.2.3.4:5"};
	char *portTwice[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link-port", "1", "--arcade-link-port", "1",
		"--arcade-link-peer", "1.2.3.4:5"};
	/* --arcade-link-peer: malformed or more than eight. */
	char *peerBad[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link-port", "1", "--arcade-link-peer", "1.2.3.4:5",
		"--arcade-link-peer", "host:5"};
	char *ninePeers[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link-port", "1", "--arcade-link-peer", "1.0.0.1:1",
		"--arcade-link-peer", "1.0.0.2:2", "--arcade-link-peer", "1.0.0.3:3", "--arcade-link-peer", "1.0.0.4:4",
		"--arcade-link-peer", "1.0.0.5:5", "--arcade-link-peer", "1.0.0.6:6", "--arcade-link-peer", "1.0.0.7:7",
		"--arcade-link-peer", "1.0.0.8:8", "--arcade-link-peer", "1.0.0.9:9"};
	/* --arcade-link-preview: unknown name or given twice. */
	char *previewUnknown[] = {"ctr_native", "--arcade-link-preview", "credits"};
	char *previewNone[] = {"ctr_native", "--arcade-link-preview", "none"};
	char *previewTwice[] = {"ctr_native", "--arcade-link-preview", "title", "--arcade-link-preview", "title"};
	/* Post-scan rules. */
	char *linkNoPort[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link-peer", "1.2.3.4:5"};
	char *linkNoPeer[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link-port", "1"};
	char *linkAlone[] = {"ctr_native", "--arcade-link", "cab2"};
	char *linkAndPreview[] = {"ctr_native", "--arcade-link", "cab1", "--arcade-link-port", "1", "--arcade-link-peer",
		"1.2.3.4:5", "--arcade-link-preview", "lobby"};
	char *portOnly[] = {"ctr_native", "--arcade-link-port", "48000"};
	char *peerOnly[] = {"ctr_native", "--arcade-link-peer", "127.0.0.1:48000"};
	char *portPeerOnly[] = {"ctr_native", "--arcade-link-port", "48000", "--arcade-link-peer", "127.0.0.1:48001"};
	char *previewAndPort[] = {"ctr_native", "--arcade-link-preview", "title", "--arcade-link-port", "48000"};
	char *previewAndPeer[] = {"ctr_native", "--arcade-link-preview", "title", "--arcade-link-peer", "127.0.0.1:48000"};
	/* A NULL entry in argv before argc, and a NULL argv. */
	char *nullEntry[] = {"ctr_native", NULL, "--arcade-link-preview", "title"};
	char *valid[] = {"ctr_native", "--arcade-link-preview", "title"};
	char **allErrors[] = {
		linkMissing, linkDash, linkNull, portMissing, portDash, peerMissing, peerDash, previewMissing, previewDash,
		previewNull, linkBadValue, linkUpper, linkTwice, linkTwiceMixed, portZero, portHigh, portLong, portAlpha,
		portPlus, portEmpty, portTwice, peerBad, ninePeers, previewUnknown, previewNone, previewTwice, linkNoPort,
		linkNoPeer, linkAlone, linkAndPreview, portOnly, peerOnly, portPeerOnly, previewAndPort, previewAndPeer,
		nullEntry,
	};
	const int allCounts[] = {
		ARGC(linkMissing), ARGC(linkDash), ARGC(linkNull), ARGC(portMissing), ARGC(portDash), ARGC(peerMissing),
		ARGC(peerDash), ARGC(previewMissing), ARGC(previewDash), ARGC(previewNull), ARGC(linkBadValue),
		ARGC(linkUpper), ARGC(linkTwice), ARGC(linkTwiceMixed), ARGC(portZero), ARGC(portHigh), ARGC(portLong),
		ARGC(portAlpha), ARGC(portPlus), ARGC(portEmpty), ARGC(portTwice), ARGC(peerBad), ARGC(ninePeers),
		ARGC(previewUnknown), ARGC(previewNone), ARGC(previewTwice), ARGC(linkNoPort), ARGC(linkNoPeer),
		ARGC(linkAlone), ARGC(linkAndPreview), ARGC(portOnly), ARGC(peerOnly), ARGC(portPeerOnly),
		ARGC(previewAndPort), ARGC(previewAndPeer), ARGC(nullEntry),
	};

	CHECK(sizeof(allErrors) / sizeof(allErrors[0]) == sizeof(allCounts) / sizeof(allCounts[0]));
	for (size_t i = 0; i < sizeof(allErrors) / sizeof(allErrors[0]); i++)
	{
		if (!RejectsUntouched(allCounts[i], allErrors[i]) || !RejectsFromDefaults(allCounts[i], allErrors[i]))
		{
			fprintf(stderr, "error case %u was accepted or wrote the options\n", (unsigned)i);
			return 1;
		}
	}
	CHECK(RejectsUntouched(3, NULL));
	CHECK(!NativeArcadeLinkOptions_ApplyArgs(ARGC(valid), valid, NULL));
	return 0;
}

static int TestFixture(void)
{
	struct NativeIdentityV1 identity;
	struct NativeIdentityV1 otherBuild;
	struct NativeIdentityV1 otherDisc;
	struct NativeIdentityV1 zeroBuild;
	struct NativeIdentityV1 zeroContent;
	struct NativeMatchConfigV1 first;
	struct NativeMatchConfigV1 second;
	struct NativeMatchConfigV1 other;
	struct NativeMatchConfigV1 snapshot;
	uint8_t firstDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t secondDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t otherDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t computed[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeSha256 sha;
	static const char text[] = NATIVE_ARCADE_LINK_FIXTURE_BOT_RULES_TEXT;

	FillIdentity(&identity, 0x11u, 0x80u);

	memset(&first, 0xA5, sizeof(first));
	CHECK(NativeArcadeLinkFixture_Build(&identity, &first));
	CHECK(NativeMatchConfigV1_Validate(&first));

	/* Exact field values. */
	CHECK(first.configurationVersion == NATIVE_MATCH_CONFIG_V1_VERSION);
	CHECK(first.profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB);
	CHECK(first.trackID == 3u);
	CHECK(first.gameMode1 == 0);
	CHECK(first.gameMode2 == 0);
	CHECK(first.rules == 0);
	CHECK(first.lapCount == 3u);
	CHECK(first.tickRateNumerator == 30u);
	CHECK(first.tickRateDenominator == 1u);
	CHECK(first.masterSeed == UINT64_C(0x4354524e41524331));
	CHECK(first.rngDerivationVersion == NATIVE_MATCH_CONFIG_V1_RNG_DERIVATION_VERSION);
	CHECK(first.canonicalSchemaVersion == NATIVE_MATCH_CONFIG_V1_CANONICAL_SCHEMA_VERSION);
	CHECK(first.replayFormatVersion == NATIVE_MATCH_CONFIG_V1_REPLAY_FORMAT_VERSION);
	CHECK(first.protocolVersion == NATIVE_MATCH_CONFIG_V1_PROTOCOL_VERSION);
	CHECK(memcmp(first.buildIdentity, identity.build, sizeof(first.buildIdentity)) == 0);
	CHECK(memcmp(first.contentIdentity, identity.content, sizeof(first.contentIdentity)) == 0);
	CHECK(first.slots[0].role == NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN);
	CHECK(first.slots[1].role == NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN);
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		const uint8_t zeroReserved[NATIVE_MATCH_CONFIG_V1_SLOT_RESERVED_BYTES] = {0};

		if (i <= 5u)
		{
			CHECK(first.slots[i].characterID == (uint8_t)i);
			CHECK(first.slots[i].initialLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);
			if (i >= 2u)
			{
				CHECK(first.slots[i].role == NATIVE_MATCH_SLOT_ROLE_BOT);
			}
		}
		else
		{
			CHECK(first.slots[i].role == NATIVE_MATCH_SLOT_ROLE_INACTIVE);
			CHECK(first.slots[i].initialLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_INACTIVE);
			CHECK(first.slots[i].characterID == 0);
		}
		CHECK(first.slots[i].difficulty == 0);
		CHECK(memcmp(first.slots[i].reserved, zeroReserved, sizeof(zeroReserved)) == 0);
	}
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_RESERVED_BYTES; i++)
	{
		CHECK(first.reserved[i] == 0);
	}

	/* botRulesDigest: the fixed external digest and a direct SHA-256 of the text. */
	CHECK(sizeof(text) - 1u == 37u);
	CHECK(memcmp(first.botRulesDigest, k_botRulesDigest, sizeof(k_botRulesDigest)) == 0);
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, text, sizeof(text) - 1u);
	NativeSha256_Final(&sha, computed);
	CHECK(memcmp(computed, k_botRulesDigest, sizeof(computed)) == 0);

	/* Deterministic: a second cabinet with the same identity builds identical bytes. */
	memset(&second, 0x5A, sizeof(second));
	CHECK(NativeArcadeLinkFixture_Build(&identity, &second));
	CHECK(memcmp(&first, &second, sizeof(first)) == 0);
	CHECK(NativeMatchConfigV1_Digest(&first, firstDigest));
	CHECK(NativeMatchConfigV1_Digest(&second, secondDigest));
	CHECK(memcmp(firstDigest, secondDigest, sizeof(firstDigest)) == 0);

	/* A different build or a different disc changes the config digest. */
	otherBuild = identity;
	otherBuild.build[31] ^= 0x01u;
	CHECK(NativeArcadeLinkFixture_Build(&otherBuild, &other));
	CHECK(NativeMatchConfigV1_Digest(&other, otherDigest));
	CHECK(memcmp(firstDigest, otherDigest, sizeof(firstDigest)) != 0);
	otherDisc = identity;
	otherDisc.content[0] ^= 0x80u;
	CHECK(NativeArcadeLinkFixture_Build(&otherDisc, &other));
	CHECK(NativeMatchConfigV1_Digest(&other, otherDigest));
	CHECK(memcmp(firstDigest, otherDigest, sizeof(firstDigest)) != 0);

	/* Zero build or content identity, and NULL arguments, leave *config untouched. */
	zeroBuild = identity;
	memset(zeroBuild.build, 0, sizeof(zeroBuild.build));
	zeroContent = identity;
	memset(zeroContent.content, 0, sizeof(zeroContent.content));
	memset(&other, 0xC3, sizeof(other));
	snapshot = other;
	CHECK(!NativeArcadeLinkFixture_Build(&zeroBuild, &other));
	CHECK(memcmp(&other, &snapshot, sizeof(other)) == 0);
	CHECK(!NativeArcadeLinkFixture_Build(&zeroContent, &other));
	CHECK(memcmp(&other, &snapshot, sizeof(other)) == 0);
	memset(&zeroBuild, 0, sizeof(zeroBuild));
	CHECK(!NativeArcadeLinkFixture_Build(&zeroBuild, &other));
	CHECK(memcmp(&other, &snapshot, sizeof(other)) == 0);
	CHECK(!NativeArcadeLinkFixture_Build(NULL, &other));
	CHECK(memcmp(&other, &snapshot, sizeof(other)) == 0);
	CHECK(!NativeArcadeLinkFixture_Build(&identity, NULL));
	return 0;
}

int main(void)
{
	CHECK(TestSetDefaults() == 0);
	CHECK(TestParsePeer() == 0);
	CHECK(TestPreviewNames() == 0);
	CHECK(TestApplyArgsValid() == 0);
	CHECK(TestApplyArgsErrors() == 0);
	CHECK(TestFixture() == 0);
	puts("native_arcade_link_options_test: ok");
	return 0;
}
