<?php
// Visitor counter for nordstjernen.org.
//
// Deploy:
//   1. Install PHP-FPM:        sudo apt install php-fpm
//   2. Make a data dir owned by the web user, e.g.:
//        sudo mkdir -p /var/lib/nordstjernen
//        sudo chown www-data:www-data /var/lib/nordstjernen
//   3. Point this script at it via the env var below (in your nginx
//      location block), or it falls back to ./.data/counter.txt next
//      to this file.
//   4. Add an nginx location block that passes counter.php to php-fpm.
//      See README or the comment at the bottom of this file.

$counterFile = getenv('NORDSTJERNEN_COUNTER_FILE');
if (!$counterFile) {
    $counterFile = __DIR__ . '/.data/counter.txt';
    @mkdir(dirname($counterFile), 0775, true);
}

$count = 0;
$fp = @fopen($counterFile, 'c+');
if ($fp !== false) {
    flock($fp, LOCK_EX);
    rewind($fp);
    $contents = stream_get_contents($fp);
    $count = (int) trim($contents);
    $count++;
    rewind($fp);
    ftruncate($fp, 0);
    fwrite($fp, (string) $count);
    fflush($fp);
    flock($fp, LOCK_UN);
    fclose($fp);
}

$digits = str_pad((string) $count, 9, '0', STR_PAD_LEFT);
$charW  = 14;
$pad    = 6;
$h      = 28;
$w      = $pad * 2 + strlen($digits) * $charW;

header('Content-Type: image/svg+xml; charset=utf-8');
header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
header('Pragma: no-cache');
header('Expires: 0');

echo '<?xml version="1.0" encoding="UTF-8"?>' . "\n";
?>
<svg xmlns="http://www.w3.org/2000/svg" width="<?= $w ?>" height="<?= $h ?>" viewBox="0 0 <?= $w ?> <?= $h ?>" shape-rendering="crispEdges">
  <rect x="0" y="0" width="<?= $w ?>" height="<?= $h ?>" fill="#000000" stroke="#ffffff" stroke-width="1"/>
  <text x="<?= $w / 2 ?>" y="<?= $h * 0.72 ?>"
        font-family="'Courier New', Courier, monospace"
        font-size="20" font-weight="bold"
        fill="#00ff66" text-anchor="middle"
        letter-spacing="3"><?= htmlspecialchars($digits, ENT_QUOTES) ?></text>
</svg>
<?php
/*
nginx location block (drop into your server { } in
/etc/nginx/sites-available/nordstjernen.org):

    location = /counter.php {
        fastcgi_pass             unix:/run/php/php-fpm.sock;
        fastcgi_index            counter.php;
        fastcgi_param            SCRIPT_FILENAME $document_root$fastcgi_script_name;
        fastcgi_param            NORDSTJERNEN_COUNTER_FILE /var/lib/nordstjernen/counter.txt;
        include                  fastcgi_params;
    }

Then:    sudo nginx -t && sudo systemctl reload nginx
*/
