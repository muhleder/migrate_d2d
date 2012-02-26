<?php

/**
 * Handling specific to a Drupal 6 source for taxonomy terms.
 */
class DrupalTerm6Migration extends DrupalTermMigration {
  public function __construct(array $arguments) {
    parent::__construct($arguments);

    // Drupal 6 had no format for terms.
    $this->addFieldMapping('format');
  }

  /**
   * Implementation of DrupalTermMigration::termQuery().
   *
   * @return SelectQueryInterface
   */
  protected function termQuery() {
    // Note the explode - this supports the (admittedly unusual) case of
    // consolidating multiple vocabularies into one.
    $query = Database::getConnection('default', $this->sourceConnection)
             ->select('term_data', 'td')
             ->fields('td', array('tid', 'name', 'description', 'weight'))
             ->condition('vid', explode(',', $this->sourceVocabulary), 'IN')
             ->orderBy('parent')
             ->distinct();
    $query->leftJoin('term_hierarchy', 'th', 'td.tid=th.tid');
    $query->fields('th', array('parent'));
    return $query;
  }
}
