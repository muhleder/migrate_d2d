<?php
// $Id: wordpress_source.inc,v 1.1.2.4 2011/02/04 19:50:40 mikeryan Exp $

/**
 * @file
 * Common source support for migration from Drupal JSON objects.
 */

class DrupalListJSON extends MigrateListJSON {
  protected $idFieldName;

  public function __construct($list_url, $http_options, $id_field_name) {
    parent::__construct($list_url, $http_options);
    $this->idFieldName = $id_field_name;
  }

  /**
   * Given an array generated from JSON, parse out the IDs for processing
   * and return them as an array. Input is an array of objects
   *
   * @param array $data
   *
   * @return array
   */
  protected function getIDsFromJSON(array $data) {
    $idlist = array();
    foreach ($data as $object) {
      $idlist[] = $object[$this->idFieldName];
    }
    return $idlist;
  }
}

class DrupalSource extends MigrateSourceList {
  public function __construct($endpoint, $token, $resource_name, $id_field_name, $fields) {
    $list_url = $endpoint . '/' . $resource_name;
    $item_url = $list_url . '/:id';
    $http_options = array('headers' => array('Cookie' => $token));
    parent::__construct(new DrupalListJSON($list_url, $http_options, $id_field_name),
      new MigrateItemJSON($item_url, $http_options), $fields);
  }
}
