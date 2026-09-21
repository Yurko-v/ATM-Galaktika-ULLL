<?php

return [
    'db' => [
        'dsn'  => 'mysql:host=localhost;dbname=CHANGE_ME;charset=utf8mb4',
        'user' => 'CHANGE_ME',
        'pass' => 'CHANGE_ME',
    ],

    'api_key' => '',

    'controller_max_age_sec' => 180,
    'controller_refresh_sec' => 20,

    'rate_limit_per_min' => 120,

    'test_positions' => [],

    'ranges' => [
        ['pattern' => '/^ULLI_(\w+_)?GND$/', 'ranges' => [['0720', '0757']]],
        ['pattern' => '/^ULLL_R[56]?_CTR$/', 'ranges' => [['0701', '0717']]],
        ['pattern' => '/^(ULLI|ULLL|ULOL)_/', 'ranges' => [['0760', '0777']]],
    ],
    'fallback' => [['0701', '0777']],

    'reserved' => ['0000', '1000', '1200', '2000', '7000', '7500', '7600', '7700'],

    'network_center'    => [59.800292, 30.262503],
    'network_radius_km' => 1000,

    'network_max_age_min' => 10,

    'release_after_min' => 10,

    'vatsim_data_url' => 'https://data.vatsim.net/v3/vatsim-data.json',

    'admins' => [
    ],
];
