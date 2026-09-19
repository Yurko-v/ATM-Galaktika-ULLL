<?php

declare(strict_types=1);

const CONTROLLER_RATINGS = [
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

const KSA_ALLOWED_RATINGS = [
    2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
];

function is_valid_controller_rating(int $ratingId): bool
{
    return array_key_exists($ratingId, CONTROLLER_RATINGS);
}

function rating_allows_ksa(?int $ratingId): bool
{
    return $ratingId !== null && in_array($ratingId, KSA_ALLOWED_RATINGS, true);
}

function controller_rating_short(int $ratingId): ?string
{
    return CONTROLLER_RATINGS[$ratingId] ?? null;
}