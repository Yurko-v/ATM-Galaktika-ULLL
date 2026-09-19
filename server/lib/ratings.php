<?php
// VATSIM controller ratings, by the network's own numeric id.
//
// The id is what VATSIM itself uses - in the data feed as "rating", and in the
// OAuth profile the production version will read - so it is the id, never the
// short name, that is stored in user_names.rating_id, checked for access,
// passed around the API and returned to the plug-in. The short names below are
// for showing to a person, and nothing else.
//
// Until OAuth is in place the rating is whatever the controller chose on the
// registration page, and an engineer can correct it on the admin page; the
// network is not asked. The ids are already the network's own, so that switch
// changes where the number comes from and nothing else.

declare(strict_types=1);

// Ratings that are not a controller rating at all: inactive, suspended, and an
// observer, who may watch but not control.
const RATING_INACTIVE  = -1;
const RATING_SUSPENDED = 0;
const RATING_OBSERVER  = 1;

// The lowest rating the КСА is open to: S1, the first that may control.
const RATING_MIN_KSA = 2;

// id => the short name, and only the short name: "S1", "SUP", "ADM" - the way
// VATSIM itself writes a rating and the way it is shown everywhere here.
const VATSIM_CONTROLLER_RATINGS = [
    RATING_INACTIVE  => 'INA',
    RATING_SUSPENDED => 'SUS',
    RATING_OBSERVER  => 'OBS',
    2                => 'S1',
    3                => 'S2',
    4                => 'S3',
    5                => 'C1',
    6                => 'C2',
    7                => 'C3',
    8                => 'I1',
    9                => 'I2',
    10               => 'I3',
    11               => 'SUP',
    12               => 'ADM',
];

// Whether the network knows a rating by this id at all. A number from outside -
// a form, a query, one day the OAuth profile - is checked with this before it
// is used as a rating.
function is_valid_controller_rating(int $ratingId): bool
{
    return array_key_exists($ratingId, VATSIM_CONTROLLER_RATINGS);
}

// Who may use the КСА: a controller rating, S1 and above. An observer, a
// suspended or an inactive account may not, and neither may an id the network
// does not know.
function rating_allows_ksa(?int $ratingId): bool
{
    return $ratingId !== null
        && is_valid_controller_rating($ratingId)
        && $ratingId >= RATING_MIN_KSA;
}

// "S1" - or null for an id the network does not know, which is never shown as
// a rating but as a dash.
function controller_rating_short(int $ratingId): ?string
{
    return VATSIM_CONTROLLER_RATINGS[$ratingId] ?? null;
}

// A rating id as it arrives from a form or a query string: the digits of an id
// the network knows, with a minus sign allowed, and null for anything else.
// The empty string - "Выберите рейтинг" still selected - is null as well.
function clean_rating_id($value): ?int
{
    $s = trim((string)$value);

    if (!preg_match('/^-?\d{1,3}$/', $s)) {
        return null;
    }

    $ratingId = (int)$s;

    return is_valid_controller_rating($ratingId) ? $ratingId : null;
}

// The ratings offered on the registration and admin pages: the ones that give
// access, id => short name, in the network's own order.
function ksa_rating_options(): array
{
    return array_filter(
        VATSIM_CONTROLLER_RATINGS,
        static fn (int $ratingId): bool => $ratingId >= RATING_MIN_KSA,
        ARRAY_FILTER_USE_KEY
    );
}
