<?php
// $Id: wordpress_category.inc,v 1.1.2.2 2011/01/05 22:19:00 mikeryan Exp $

/**
 * @file
 *
 * Support for migrating categories from a WordPress blog into Drupal.
 */

/**
 * Implementation of DrupalMigration, for terms
 */
class DrupalTerm extends DrupalMigration {
  protected $sourceVid;

  /**
   * Set it up
   */
  public function __construct(array $arguments = array()) {
    $this->sourceVid = $arguments['source_vid'];
    parent::__construct($arguments);

    $fields = array(
      'tid' => '',
      'vid' => '',
      'name' => '',
      'description' => '',
      'weight' => '',
    );

    $this->map = new MigrateSQLMap($this->machineName,
      array(
        'tid' => array(
          'type' => 'int',
          'unsigned' => TRUE,
          'not null' => TRUE,
          'description' => 'Term ID',
        )
      ),
      MigrateDestinationTerm::getKeySchema()
    );

    // Construct the source and destination objects.
    $this->source = new DrupalSource($this->getEndpoint(), $this->getToken(),
      'taxonomy_term', 'tid', $fields);
    $this->destination = new MigrateDestinationTerm($arguments['bundle']);

    // The basic mappings
    $this->addFieldMapping('name', 'name');
    $this->addFieldMapping('description', 'description');
    $this->addFieldMapping('weight', 'weight');

    // Unmapped destination fields
    $this->addFieldMapping('parent_name');
    $this->addFieldMapping('parent');
    $this->addFieldMapping('format');
    $this->addFieldMapping('path');

    // Unmapped source fields
    $this->addFieldMapping(NULL, 'vid');
  }

  /**
   * Machine name is class name plus the source vid.
   */
  protected function generateMachineName() {
    $class_name = get_class($this);
    return $class_name . $this->sourceVid;
  }

  public function prepareRow($row) {
    if ($row->vid != $this->sourceVid) {
      return FALSE;
    }
  }
}
