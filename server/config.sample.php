<?php
// Galaxy ATM System - squawk server settings.
// Copy this file to config.php (next to it) and fill it in. config.php holds
// the database password, so it is never committed.
//
// This is the only place a secret lives. The plug-in carries none: a request is
// let in because the position it names is really controlling on VATSIM at that
// moment (see require_caller() in lib/bootstrap.php).

return [
    'db' => [
        'dsn'  => 'mysql:host=localhost;dbname=CHANGE_ME;charset=utf8mb4',
        'user' => 'CHANGE_ME',
        'pass' => 'CHANGE_ME',
    ],

    // Optional second lock, off when empty - a shared key every plug-in must
    // send as X-Api-Key on top of being online. Leave it empty: a key shared by
    // everyone is a key that leaks, and it buys nothing the online check does
    // not already give. Set it only to close the service off entirely for a
    // while - and then every controller needs the key file as well.
    'api_key' => '',

    // How old the list of who is controlling may be before nobody is let in at
    // all. The cron refreshes it every minute; an endpoint that cannot find its
    // caller in it refreshes it itself, at most once every few seconds.
    'controller_max_age_sec' => 180,
    'controller_refresh_sec' => 20,

    // Requests one position may make in a minute; 0 turns the limit off. Not a
    // lock - just a bound on what a plug-in stuck in a loop can cost.
    'rate_limit_per_min' => 120,

    // Positions let in without being online - for checking the server from the
    // command line (see README, step 9). Leave this empty in normal use: a
    // callsign listed here can be used by anyone who knows the address.
    'test_positions' => [],

    // Code ranges per position, from the sector file (.ese, [POSITIONS]).
    // The first pattern matching the requesting position's callsign wins and
    // its ranges are tried first; 'fallback' is tried after them, and is all a
    // position matching no pattern gets. Codes are octal - digits 0-7 only.
    'ranges' => [
        ['pattern' => '/^ULLI_(\w+_)?GND$/', 'ranges' => [['0720', '0757']]],
        ['pattern' => '/^ULLL_R[56]?_CTR$/', 'ranges' => [['0701', '0717']]],
        ['pattern' => '/^(ULLI|ULLL|ULOL)_/', 'ranges' => [['0760', '0777']]],
    ],
    'fallback' => [['0701', '0777']],

    // Never handed out, whatever the ranges say.
    'reserved' => ['0000', '1000', '1200', '2000', '7000', '7500', '7600', '7700'],

    // A code squawked or assigned by any pilot online within this radius of
    // the centre counts as taken, whoever gave it - neighbouring FIRs included.
    'network_center'    => [59.800292, 30.262503],   // ULLI
    'network_radius_km' => 1000,

    // The network snapshot stops counting once it is older than this - a cron
    // that has stopped must not keep codes blocked, or released, on old data.
    'network_max_age_min' => 10,

    // A code stays with its aircraft until the pilot has been off the network
    // this long - room for a reconnect.
    'release_after_min' => 10,

    'vatsim_data_url' => 'https://data.vatsim.net/v3/vatsim-data.json',

    // Logins for the admin page (public/admin/, see README "Админ-страница"):
    // who may add, correct and delete controllers in user_names - and nothing
    // else in the database. Login => password HASH, never the password itself;
    // README gives the one command that makes a hash. Single quotes around it -
    // it is full of "$". Deleting a line or changing its hash ends every open
    // session of that login on its next click. Empty: the page lets nobody in.
    'admins' => [
        // 'ivanov' => '$2y$10$...',
    ],
];
