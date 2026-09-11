<?php
// Galaxy ATM System - squawk server settings.
// Copy this file to config.php (next to it) and fill it in. config.php holds
// the database password and the API key, so it is never committed.

return [
    'db' => [
        'dsn'  => 'mysql:host=localhost;dbname=CHANGE_ME;charset=utf8mb4',
        'user' => 'CHANGE_ME',
        'pass' => 'CHANGE_ME',
    ],

    // Shared by every controller's plugin (GalaxyATMSystem.json -> Squawk.ApiKey).
    // Any long random string.
    'api_key' => 'CHANGE_ME',

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
];
