<?php

declare(strict_types=1);

const VATSIM_CONTROLLER_RATINGS = [
    -1 => 'INA',
    0  => 'SUS',
    1  => 'OBS',
    2  => 'S1',
    3  => 'S2',
    4  => 'S3',
    5  => 'C1',
    6  => 'C2',
    7  => 'C3',
    8  => 'I1',
    9  => 'I2',
    10 => 'I3',
    11 => 'SUP',
    12 => 'ADM',
];

function is_valid_controller_rating(int $ratingId): bool
{
    return array_key_exists($ratingId, VATSIM_CONTROLLER_RATINGS);
}

function rating_allows_ksa(?int $ratingId): bool
{
    return $ratingId !== null && $ratingId >= 2 && $ratingId <= 12;
}

function controller_rating_short(int $ratingId): ?string
{
    return VATSIM_CONTROLLER_RATINGS[$ratingId] ?? null;
}

function ksa_rating_options(): array
{
    return array_filter(
        VATSIM_CONTROLLER_RATINGS,
        static fn (int $ratingId): bool => $ratingId >= 2,
        ARRAY_FILTER_USE_KEY
    );
}